#include "epoll_wrapper.h"
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cmath>
#include <stdexcept>

// Парсер (тот же, что в сервере) для локальной проверки
class ExprParser {
    const std::string &s;
    size_t i;
public:
    ExprParser(const std::string &str) : s(str), i(0) {}
    double parse() {
        return parseExpression();
    }
private:
    void skipSpaces() {
        while (i < s.size() && s[i] == ' ') ++i;
    }
    double parseNumber() {
        skipSpaces();
        size_t start = i;
        while (i < s.size() && (isdigit(s[i]) || s[i]=='.')) ++i;
        return std::stod(s.substr(start, i - start));
    }
    double parseFactor() {
        skipSpaces();
        if (s[i] == '(') {
            ++i;
            double v = parseExpression();
            skipSpaces();
            ++i; // пропустить ')'
            return v;
        }
        return parseNumber();
    }
    double parseTerm() {
        double lhs = parseFactor();
        while (true) {
            skipSpaces();
            if (i>=s.size()) break;
            char op = s[i];
            if (op!='*' && op!='/') break;
            ++i;
            double rhs = parseFactor();
            lhs = (op=='*' ? lhs*rhs : lhs/rhs);
        }
        return lhs;
    }
    double parseExpression() {
        double lhs = parseTerm();
        while (true) {
            skipSpaces();
            if (i>=s.size()) break;
            char op = s[i];
            if (op!='+' && op!='-') break;
            ++i;
            double rhs = parseTerm();
            lhs = (op=='+' ? lhs+rhs : lhs-rhs);
        }
        return lhs;
    }
};

// Генерация выражения с пробелами между токенами
std::string gen_expr(int n) {
    std::ostringstream ss;
    static std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(1, 100);
    std::uniform_int_distribution<int> op(0, 3);
    const char ops[] = {'+','-','*','/'};
    ss << dist(rng);
    for (int i = 1; i < n; ++i) {
        ss << ' ' << ops[op(rng)] << ' ' << dist(rng);
    }
    return ss.str();
}

struct Session {
    std::string expr;
    double correct;
    size_t idx = 0;
    int chunk_count = 0;
    std::string recv_buf;
};

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0]
                  << " <n> <connections> <server_addr> <server_port>\n";
        return 1;
    }
    int n = std::stoi(argv[1]);
    int connections = std::stoi(argv[2]);
    std::string addr = argv[3];
    int port = std::stoi(argv[4]);

    std::cout << "Client: n="<<n
              <<", sessions="<<connections
              <<", server="<<addr<<":"<<port<<"\n"
              <<"----------------------------------------\n";

    Epoll ep;
    std::unordered_map<int, Session> sessions;

    // Создаём все клиентские сессии
    for (int i = 0; i < connections; ++i) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        set_nonblocking(sock);
        sockaddr_in s{};
        s.sin_family = AF_INET;
        s.sin_port = htons(port);
        inet_pton(AF_INET, addr.c_str(), &s.sin_addr);
        connect(sock, (sockaddr*)&s, sizeof(s)); // неблокирующий connect

        Session sess;
        sess.expr = gen_expr(n);
        sess.correct = ExprParser(sess.expr).parse();
        sessions[sock] = sess;

        ep.add(sock, EPOLLOUT | EPOLLIN);
        std::cout << "[FD="<<sock<<"] Expr='"<<sess.expr
                  <<"' expected="<<sess.correct<<"\n";
    }

    epoll_event events[64];

    while (!sessions.empty()) {
        int nfds = ep.wait(events, 64, -1);
        for (int j = 0; j < nfds; ++j) {
            int fd = events[j].data.fd;
            auto &sess = sessions[fd];

            // отправка фрагментов
            if ((events[j].events & EPOLLOUT) && sess.idx < sess.expr.size()) {
                int maxCh = std::min(10, int(sess.expr.size() - sess.idx));
                static std::mt19937_64 rng(std::random_device{}());
                std::uniform_int_distribution<int> distFrag(1, maxCh);
                int chunk = distFrag(rng);
                int sent = send(fd, sess.expr.data() + sess.idx, chunk, 0);
                if (sent > 0) {
                    sess.idx += sent; sess.chunk_count++;
                    std::cout << "[FD="<<fd<<"] Sent chunk "<<sess.chunk_count
                              <<" ('"<<sess.expr.substr(sess.idx-sent, sent)<<"')\n";
                    if (sess.idx >= sess.expr.size()) shutdown(fd, SHUT_WR);
                }
            }

            // приём ответа
            if (events[j].events & EPOLLIN) {
                char buf[128];
                int r = recv(fd, buf, sizeof(buf)-1, 0);
                if (r > 0) {
                    buf[r] = '\0';
                    sess.recv_buf += buf;
                } else {
                    double serv = std::stod(sess.recv_buf);
                    std::cout << "[FD="<<fd<<"] Received="<<serv<<"\n";
                    if (std::abs(serv - sess.correct) < 1e-6)
                        std::cout<<"[FD="<<fd<<"] ✔ OK\n";
                    else
                        std::cerr<<"[FD="<<fd<<"] ✘ Mismatch: server="
                                 <<serv<<" expected="<<sess.correct<<"\n";
                    ep.remove(fd);
                    close(fd);
                    sessions.erase(fd);
                }
            }
        }
    }

    std::cout<<"All sessions completed\n";
    return 0;
}