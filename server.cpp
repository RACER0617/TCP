// server.cpp
#include "epoll_wrapper.h"
#include <iostream>
#include <string>
#include <unordered_map>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <stdexcept>

// Функции парсинга с учётом приоритета операций
class ExprParser {
    const std::string &s;
    size_t i;
public:
    ExprParser(const std::string &str) : s(str), i(0) {}
    double parse() {
        double res = parseExpression();
        skipSpaces();
        if (i != s.size()) throw std::runtime_error("Unexpected chars at end");
        return res;
    }
private:
    void skipSpaces() {
        while (i < s.size() && s[i] == ' ') ++i;
    }
    double parseNumber() {
        skipSpaces();
        size_t start = i;
        while (i < s.size() && (isdigit(s[i]) || s[i]=='.')) ++i;
        if (start == i) throw std::runtime_error("Number expected");
        return std::stod(s.substr(start, i - start));
    }
    double parseFactor() {
        skipSpaces();
        if (s[i] == '(') {
            ++i;
            double v = parseExpression();
            skipSpaces();
            if (s[i] != ')') throw std::runtime_error("')' expected");
            ++i;
            return v;
        }
        return parseNumber();
    }
    double parseTerm() {
        double lhs = parseFactor();
        while (true) {
            skipSpaces();
            if (i >= s.size()) break;
            char op = s[i];
            if (op!='*' && op!='/') break;
            ++i;
            double rhs = parseFactor();
            if (op == '*') lhs *= rhs;
            else {
                if (rhs == 0) throw std::runtime_error("Division by zero");
                lhs /= rhs;
            }
        }
        return lhs;
    }
    double parseExpression() {
        double lhs = parseTerm();
        while (true) {
            skipSpaces();
            if (i >= s.size()) break;
            char op = s[i];
            if (op!='+' && op!='-') break;
            ++i;
            double rhs = parseTerm();
            if (op == '+') lhs += rhs;
            else lhs -= rhs;
        }
        return lhs;
    }
};

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

struct ClientSession {
    std::string recv_buf;
    std::string send_buf;
    size_t send_offset = 0;
};

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <port>\n";
        return 1;
    }
    int port = std::stoi(argv[1]);
    std::cout << "Starting server on port " << port << "...\n";

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    bind(listen_fd, (sockaddr*)&addr, sizeof(addr));
    listen(listen_fd, 10);
    set_nonblocking(listen_fd);

    Epoll ep;
    ep.add(listen_fd, EPOLLIN);

    std::unordered_map<int, ClientSession> sessions;
    epoll_event events[64];

    while (true) {
        int ne = ep.wait(events, 64, -1);
        for (int ei = 0; ei < ne; ++ei) {
            int fd = events[ei].data.fd;
            uint32_t ev = events[ei].events;

            if (fd == listen_fd) {
                // новое соединение
                int client = accept(listen_fd, nullptr, nullptr);
                set_nonblocking(client);
                ep.add(client, EPOLLIN);
                sessions[client] = {};
                std::cout << "[CONN] Accepted fd=" << client << "\n";
                continue;
            }

            auto &sess = sessions[fd];
            if (ev & EPOLLIN) {
                // читаем всё до EOF или EAGAIN
                char buf[1024];
                bool closed = false;
                while (true) {
                    ssize_t r = read(fd, buf, sizeof(buf));
                    if (r > 0) sess.recv_buf.append(buf, r);
                    else if (r == 0) { closed = true; break; }
                    else if (errno == EAGAIN) break;
                    else { perror("read"); closed = true; break; }
                }
                if (closed && !sess.recv_buf.empty()) {
                    std::cout << "[CLIENT fd="<<fd<<"] Expr='"<<sess.recv_buf<<"'\n";
                    try {
                        ExprParser p(sess.recv_buf);
                        double res = p.parse();
                        sess.send_buf = std::to_string(res);
                    } catch (...) {
                        sess.send_buf = "ERROR";
                    }
                    sess.send_offset = 0;
                    ep.remove(fd);
                    ep.add(fd, EPOLLOUT);
                }
            }
            if (ev & EPOLLOUT) {
                // отправляем ответ
                while (sess.send_offset < sess.send_buf.size()) {
                    ssize_t w = write(fd,
                        sess.send_buf.data() + sess.send_offset,
                        sess.send_buf.size() - sess.send_offset);
                    if (w > 0) sess.send_offset += w;
                    else if (errno == EAGAIN) break;
                    else { perror("write"); break; }
                }
                if (sess.send_offset >= sess.send_buf.size()) {
                    std::cout << "[CLIENT fd="<<fd<<"] Response sent, closing\n";
                    ep.remove(fd);
                    close(fd);
                    sessions.erase(fd);
                }
            }
        }
    }
    close(listen_fd);
    return 0;
}