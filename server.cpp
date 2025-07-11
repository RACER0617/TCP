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
    ss >> x;  // читаем первое число
    // цикл чтения операторов и следующих чисел
    while (ss >> op >> y) {
        switch(op) {
            case '+': x += y; break;  // сложение
            case '-': x -= y; break;  // вычитание
            case '*': x *= y; break;  // умножение
            case '/': x /= y; break;  // деление
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
    // Создаём сокет для прослушивания
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    bind(listen_fd, (sockaddr*)&addr, sizeof(addr));
    listen(listen_fd, 10);  // начинаем прослушку

    Epoll ep;
    ep.add(listen_fd, EPOLLIN);  // следим за входящими подключениями

    const int MAXE = 64;
    epoll_event events[MAXE];

    while (true) {
        int n = ep.wait(events, MAXE, -1);  // ждём событий бесконечно
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (fd == listen_fd) {
                // новое подключение
                int client = accept(listen_fd, nullptr, nullptr);
                ep.add(client, EPOLLIN);
            } else if (events[i].events & EPOLLIN) {
                // данные от клиента
                char buf[1024];
                int r = read(fd, buf, sizeof(buf)-1);
                if (r <= 0) {
                    // клиент закрыл соединение
                    ep.remove(fd);
                    close(fd);
                } else {
                    buf[r] = '\0';
                    double res = evaluate(buf);
                    std::string out = std::to_string(res);
                    write(fd, out.c_str(), out.size());  // отправляем результат
                }
            }
        }
    }
    return 0;
}