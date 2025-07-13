#include "epoll_wrapper.h"
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>
#include <unordered_map>
#include <cerrno>

// Сессия клиента на сервере
struct ClientSession {
    std::string recv_buf;
    std::string send_buf;
    size_t send_offset = 0;
};

// Функция вычисления арифметического выражения без пробелов
static double evaluate(const std::string &expr) {
    std::istringstream ss(expr);
    double x, y; char op;
    ss >> x;
    while (ss >> op >> y) {
        if (op == '/' && y == 0) {
            throw std::runtime_error("Division by zero");
        }
        switch(op) {
            case '+': x += y; break;
            case '-': x -= y; break;
            case '*': x *= y; break;
            case '/': x /= y; break;
            default: break;
        }
    }
    return x;
}

// Установка неблокирующего режима для сокета
static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <port>\n";
        return 1;
    }
    int port = std::stoi(argv[1]);
    std::cout << "Starting server on port " << port << "...\n";

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(listen_fd, 10) < 0) { perror("listen"); return 1; }

    // Установим listen в неблокирующий
    set_nonblocking(listen_fd);

    Epoll ep;
    ep.add(listen_fd, EPOLLIN);

    std::unordered_map<int, ClientSession> sessions;
    const int MAXE = 64;
    epoll_event events[MAXE];
    int conn_id = 0;

    while (true) {
        int ne = ep.wait(events, MAXE, -1);
        if (ne < 0) { perror("epoll_wait"); break; }
        for (int i = 0; i < ne; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (fd == listen_fd) {
                // Новое соединение
                int client = accept(listen_fd, nullptr, nullptr);
                if (client >= 0) {
                    set_nonblocking(client);
                    ep.add(client, EPOLLIN);
                    sessions[client] = ClientSession{};
                    std::cout << "[CONNECTION #" << ++conn_id
                              << "] Accepted client fd=" << client << "\n";
                }
                continue;
            }

            auto &sess = sessions[fd];

            // Чтение
            if (ev & EPOLLIN) {
                bool closed_by_client = false;
                char buf[1024];
                while (true) {
                    ssize_t r = read(fd, buf, sizeof(buf));
                    if (r > 0) {
                        sess.recv_buf.append(buf, r);
                    } else if (r == 0) {
                        // клиент вызвал shutdown SHUT_WR
                        closed_by_client = true;
                        break;
                    } else {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        perror("read");
                        closed_by_client = true;
                        break;
                    }
                }

                if (closed_by_client && !sess.recv_buf.empty()) {
                    std::cout << "[CLIENT fd=" << fd << "] Expr='" << sess.recv_buf << "'\n";
                    try {
                        double res = evaluate(sess.recv_buf);
                        sess.send_buf = std::to_string(res);
                    } catch (const std::exception &ex) {
                        sess.send_buf = "ERROR";
                    }
                    sess.send_offset = 0;
                    // Переключаемся на отправку ответа
                    ep.remove(fd);
                    ep.add(fd, EPOLLOUT);
                }
            }

            // Отправка
            if (ev & EPOLLOUT) {
                while (sess.send_offset < sess.send_buf.size()) {
                    ssize_t w = write(fd,
                        sess.send_buf.data() + sess.send_offset,
                        sess.send_buf.size() - sess.send_offset);
                    if (w > 0) {
                        sess.send_offset += w;
                    } else if (w < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        perror("write");
                        break;
                    }
                }
                if (sess.send_offset >= sess.send_buf.size()) {
                    ep.remove(fd);
                    close(fd);
                    sessions.erase(fd);
                    std::cout << "[CLIENT fd=" << fd << "] Response sent and closed\n";
                }
            }
        }
    }
    close(listen_fd);
    return 0;
}