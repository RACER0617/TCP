#pragma once
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <mswsock.h>
    #include <vector>
#else
    #include <sys/epoll.h>
#endif

// Класс-обёртка для работы с epoll на Linux и WSAPoll на Windows
class Epoll {
public:
    Epoll();             // Конструктор инициализации
    ~Epoll();            // Деструктор очистки
    bool add(int fd, uint32_t events);  // Добавление дескриптора в набор
    bool remove(int fd);               // Удаление дескриптора
    int wait(struct epoll_event *events, int maxevents, int timeout); // Ожидание событий
private:
#ifdef _WIN32
    std::vector<WSAPOLLFD> fds; // Список WSAPOLLFD для WSAPoll
#else
    int epfd; // Дескриптор epoll
#endif
};