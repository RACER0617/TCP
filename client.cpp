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
#include <cmath>
#include <cstring>
#include <fcntl.h>


struct Session {
    std::string expr;
    double correct;
    size_t idx = 0;
    int chunk_count = 0;
    std::string recv_buf;
};

// Генерация случайного выражения
std::string gen_expr(int n) {
    std::ostringstream ss;
    static std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(1, 100);
    std::uniform_int_distribution<int> op(0, 3);
    const char ops[] = {'+', '-', '*', '/'};
    ss << dist(rng);
    for (int i = 1; i < n; ++i) {
        ss << ops[op(rng)] << dist(rng);
    }
    return ss.str();
}

// Локальное вычисление
double eval_local(const std::string &e) {
    std::istringstream ss(e);
    double x, y; char o;
    ss >> x;
    while (ss >> o >> y) {
        switch (o) {
            case '+': x += y; break;
            case '-': x -= y; break;
            case '*': x *= y; break;
            case '/': x /= y; break;
            default: break;
        }
    }
    return x;
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

    std::cout << "Client: n=" << n
              << ", sessions=" << connections
              << ", server=" << addr << ":" << port << "\n"
              << "----------------------------------------\n";

    Epoll ep;
    std::unordered_map<int, Session> sessions;

    // Создаем сессии и неблокирующие сокеты
    for (int i = 0; i < connections; ++i) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        fcntl(sock, F_SETFL, O_NONBLOCK);
        sockaddr_in s{};
        s.sin_family = AF_INET;
        s.sin_port = htons(port);
        inet_pton(AF_INET, addr.c_str(), &s.sin_addr);
        connect(sock, (sockaddr*)&s, sizeof(s)); // non-blocking connect

        Session sess;
        sess.expr = gen_expr(n);
        sess.correct = eval_local(sess.expr);
        sessions[sock] = sess;

        ep.add(sock, EPOLLOUT | EPOLLIN);
        std::cout << "[FD=" << sock << "] Expr='" << sessions[sock].expr
                  << "' expected=" << sessions[sock].correct << "\n";
    }

    const int MAX_EVENTS = 64;
    epoll_event events[MAX_EVENTS];

    while (!sessions.empty()) {
        int nfds = ep.wait(events, MAX_EVENTS, -1);
        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            auto &sess = sessions[fd];
            if ((events[i].events & EPOLLOUT) && sess.idx < sess.expr.size()) {
                int maxCh = std::min(10, int(sess.expr.size() - sess.idx));
                static std::mt19937_64 rng(std::random_device{}());
                std::uniform_int_distribution<int> distFrag(1, maxCh);
                int chunk = distFrag(rng);
                int sent = send(fd, sess.expr.data() + sess.idx, chunk, 0);
                if (sent > 0) {
                    sess.idx += sent;
                    sess.chunk_count++;
                    std::cout << "[FD=" << fd << "] Sent chunk "
                              << sess.chunk_count << " ('"
                              << sess.expr.substr(sess.idx - sent, sent) << "')\n";
                    if (sess.idx >= sess.expr.size()) {
                        shutdown(fd, SHUT_WR);
                    }
                }
            }
            if (events[i].events & EPOLLIN) {
                char buf[128];
                int r = recv(fd, buf, sizeof(buf) - 1, 0);
                if (r > 0) {
                    buf[r] = '\0';
                    sess.recv_buf += buf;
                } else {
                    // конец передачи
                    double serv = std::stod(sess.recv_buf);
                    std::cout << "[FD=" << fd << "] Received=" << serv << "\n";
                    if (std::abs(serv - sess.correct) < 1e-6) {
                        std::cout << "[FD=" << fd << "] ✔ OK\n";
                    } else {
                        std::cerr << "[FD=" << fd << "] ✘ Mismatch! server="
                                  << serv << " expected=" << sess.correct << "\n";
                    }
                    ep.remove(fd);
                    close(fd);
                    sessions.erase(fd);
                }
            }
        }
    }

    std::cout << "All sessions completed\n";
    return 0;
}