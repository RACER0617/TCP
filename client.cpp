#include "epoll_wrapper.h"  // если не нужен в клиенте, можно убрать
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cmath>
#include <mutex>

std::mutex cout_mtx;

// Генерация случайного выражения
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

void session(const std::string &addr, int port, int n) {
    {
        std::lock_guard<std::mutex> lk(cout_mtx);
        std::cout << "[THREAD " << std::this_thread::get_id() << "] Starting session\n";
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in s{};
    s.sin_family = AF_INET;
    s.sin_port = htons(port);
    inet_pton(AF_INET, addr.c_str(), &s.sin_addr);

    {
        std::lock_guard<std::mutex> lk(cout_mtx);
        std::cout << "[THREAD " << std::this_thread::get_id()
                  << "] Connecting to " << addr << ":" << port << "...\n";
    }
    if (connect(sock, (sockaddr*)&s, sizeof(s)) != 0) {
        std::lock_guard<std::mutex> lk(cout_mtx);
        perror("connect");
        close(sock);
        return;
    }

    std::string expr = gen_expr(n);
    double correct = eval_local(expr);

    {
        std::lock_guard<std::mutex> lk(cout_mtx);
        std::cout << "[THREAD " << std::this_thread::get_id()
                  << "] Expr=\"" << expr << "\" (expected=" << correct << ")\n";
    }

    // Отправка по фрагментам
    std::mt19937_64 rng(std::random_device{}());
    int len = expr.size(), idx = 0, cnt = 0;
    while (idx < len) {
        int maxCh = std::min(10, len - idx);
        std::uniform_int_distribution<int> distFrag(1, maxCh);
        int chunk = distFrag(rng);
        int sent = send(sock, expr.data() + idx, chunk, 0);
        if (sent <= 0) { perror("send"); close(sock); return; }
        {
            std::lock_guard<std::mutex> lk(cout_mtx);
            std::cout << "[THREAD " << std::this_thread::get_id()
                      << "] Sent chunk " << ++cnt << " (" << sent
                      << " bytes): '" << expr.substr(idx, sent) << "'\n";
        }
        idx += sent;
    }

    // Завершаем отправку
    shutdown(sock, SHUT_WR);

    // Приём ответа
    char buf[128];
    int r = recv(sock, buf, sizeof(buf)-1, 0);
    if (r > 0) {
        buf[r] = '\0';
        double serv = std::stod(buf);
        {
            std::lock_guard<std::mutex> lk(cout_mtx);
            std::cout << "[THREAD " << std::this_thread::get_id()
                      << "] Received: " << serv << "\n";
            if (std::abs(serv - correct) < 1e-6) {
                std::cout << "[THREAD " << std::this_thread::get_id()
                          << "] ✔ OK\n";
            } else {
                std::cout << "[THREAD " << std::this_thread::get_id()
                          << "] ✘ Mismatch! expected=" << correct << "\n";
            }
        }
    } else {
        std::lock_guard<std::mutex> lk(cout_mtx);
        std::cerr << "[THREAD " << std::this_thread::get_id()
                  << "] recv error or connection closed\n";
    }

    close(sock);
    {
        std::lock_guard<std::mutex> lk(cout_mtx);
        std::cout << "[THREAD " << std::this_thread::get_id()
                  << "] Session done\n";
    }
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0]
                  << " <n> <connections> <server_addr> <server_port>\n";
        return 1;
    }
    int n           = std::stoi(argv[1]);
    int connections = std::stoi(argv[2]);
    std::string addr= argv[3];
    int port        = std::stoi(argv[4]);

    std::cout << "Client: n=" << n
              << ", sessions=" << connections
              << ", server=" << addr << ":" << port << "\n"
              << "----------------------------------------\n";

    std::vector<std::thread> th;
    th.reserve(connections);
    for (int i = 0; i < connections; ++i)
        th.emplace_back(session, addr, port, n);
    for (auto &t : th) t.join();

    std::cout << "All sessions completed\n";
    return 0;
}
