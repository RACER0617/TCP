#include "epoll_wrapper.h"
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

// Функция вычисления арифметического выражения без пробелов
static double evaluate(const std::string &expr) {
    std::istringstream ss(expr);
    double x, y; char op;
    ss >> x;
    while (ss >> op >> y) {
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

    std::cout << "Listening socket created (fd=" << listen_fd << ")\n";

    Epoll ep;
    ep.add(listen_fd, EPOLLIN);
    std::cout << "Epoll instance created. Waiting for connections...\n";

    const int MAXE = 64;
    epoll_event events[MAXE];
    int conn_id = 0;

    while (true) {
        int n = ep.wait(events, MAXE, -1);
        if (n < 0) { perror("epoll_wait"); break; }
        std::cout << "\nEpoll returned " << n << " events\n";

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;

            if (fd == listen_fd) {
                int client = accept(listen_fd, nullptr, nullptr);
                if (client >= 0) {
                    ep.add(client, EPOLLIN);
                    std::cout << "[CONNECTION #" << ++conn_id
                              << "] Accepted new client (fd=" << client << ")\n";
                }
            }
            else if (events[i].events & EPOLLIN) {
                // Считываем до EOF
                std::string full;
                char buf[1024];
                while (true) {
                    int r = read(fd, buf, sizeof(buf));
                    if (r > 0) {
                        full.append(buf, r);
                    } else if (r == 0) {
                        // клиент завершил отправку
                        break;
                    } else {
                        perror("read");
                        full.clear();
                        break;
                    }
                }

                if (full.empty()) {
                    std::cout << "[CLIENT fd=" << fd << "] Connection closed before data\n";
                } else {
                    std::cout << "[CLIENT fd=" << fd << "] Full expr: " << full << "\n";
                    double res = evaluate(full);
                    std::string out = std::to_string(res);
                    std::cout << "[CLIENT fd=" << fd << "] Computed result: " << out << "\n";
                    write(fd, out.c_str(), out.size());
                    std::cout << "[CLIENT fd=" << fd << "] Sent response\n";
                }

                ep.remove(fd);
                close(fd);
                std::cout << "[CLIENT fd=" << fd << "] Closed\n";
            }
        }
    }

    close(listen_fd);
    return 0;
}
