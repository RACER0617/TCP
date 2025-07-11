#include "epoll_wrapper.h"
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cmath>    // для std::abs

// Генерация случайного выражения из n чисел без пробелов
std::string gen_expr(int n) {
    std::ostringstream ss;
    std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(1, 100);
    std::uniform_int_distribution<int> op(0, 3);
    const char ops[] = {'+', '-', '*', '/'};
    ss << dist(rng);
    for (int i = 1; i < n; ++i) {
        ss << ops[op(rng)] << dist(rng);
    }
    return ss.str();
}

// Локальное вычисление выражения для проверки
double eval_local(const std::string &e) {
    std::istringstream ss(e);
    double x, y;
    char o;
    ss >> x;
    while (ss >> o >> y) {
        switch (o) {
            case '+': x += y; break;
            case '-': x -= y; break;
            case '*': x *= y; break;
            case '/': x /= y; break;
        }
    }
    return x;
}

// Одна TCP‑сессия: открыть сокет, отправить фрагменты, прочитать ответ
void session(const std::string &addr, int port, int n) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in s{};
    s.sin_family = AF_INET;
    s.sin_port = htons(port);
    inet_pton(AF_INET, addr.c_str(), &s.sin_addr);
    if (connect(sock, reinterpret_cast<sockaddr*>(&s), sizeof(s)) != 0) {
        std::cerr << "Connect failed\n";
        close(sock);
        return;
    }

    std::string expr = gen_expr(n);
    double correct = eval_local(expr);

    // Разбиваем строку на случайные куски
    std::mt19937_64 rng(std::random_device{}());
    int len = static_cast<int>(expr.size());
    int idx = 0;
    while (idx < len) {
        int chunk = std::uniform_int_distribution<int>(1, std::min(10, len - idx))(rng);
        send(sock, expr.data() + idx, chunk, 0);
        idx += chunk;
    }

    // Читаем ответ сервера
    char buf[128];
    int r = recv(sock, buf, sizeof(buf) - 1, 0);
    if (r > 0) {
        buf[r] = '\0';  // корректный нуль-терминатор
        double serv = std::stod(buf);
        if (std::abs(serv - correct) > 1e-6) {
            std::cerr << "Expr=" << expr
                      << " got="  << serv
                      << " expected=" << correct << "\n";
        }
    }
    close(sock);
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

    std::vector<std::thread> threads;
    threads.reserve(connections);
    for (int i = 0; i < connections; ++i) {
        threads.emplace_back(session, addr, port, n);
    }
    for (auto &t : threads) {
        t.join();
    }
    return 0;
}
