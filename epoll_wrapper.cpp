#include "epoll_wrapper.h"
#ifdef _WIN32
#include <stdexcept>

// Инициализация Winsock
Epoll::Epoll() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
}
// Очистка Winsock перед выходом
Epoll::~Epoll() {
    WSACleanup();
}
// Добавление сокета в faux-epoll через WSAPoll
bool Epoll::add(int fd, uint32_t events) {
    WSAPOLLFD pfd;
    pfd.fd = (SOCKET)fd;
    pfd.events = 0;
    if (events & EPOLLIN) pfd.events |= POLLRDNORM;
    if (events & EPOLLOUT) pfd.events |= POLLWRNORM;
    fds.push_back(pfd);
    return true;
}
// Удаление сокета из списка
bool Epoll::remove(int fd) {
    for (auto it = fds.begin(); it != fds.end(); ++it) {
        if (it->fd == (SOCKET)fd) {
            fds.erase(it);
            return true;
        }
    }
    return false;
}
// Ожидание событий WSAPoll
int Epoll::wait(epoll_event *events, int maxevents, int timeout) {
    int ret = WSAPoll(fds.data(), (ULONG)fds.size(), timeout);
    if (ret <= 0) return ret;
    int count = 0;
    for (size_t i = 0; i < fds.size() && count < maxevents; ++i) {
        uint32_t re = 0;
        if (fds[i].revents & POLLRDNORM) re |= EPOLLIN;
        if (fds[i].revents & POLLWRNORM) re |= EPOLLOUT;
        if (re) {
            events[count].data.fd = fds[i].fd;
            events[count].events = re;
            ++count;
        }
    }
    return count;
}
#else
#include <unistd.h>
#include <stdexcept>
#include <errno.h>

// Создание epoll-дескриптора
Epoll::Epoll() {
    epfd = epoll_create1(0);
    if (epfd < 0) throw std::runtime_error("epoll_create1 failed");
}
// Закрытие epoll-дескриптора
Epoll::~Epoll() {
    close(epfd);
}
// Добавление файла/сокета в epoll
bool Epoll::add(int fd, uint32_t events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == 0;
}
// Удаление файла/сокета из epoll
bool Epoll::remove(int fd) {
    return epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr) == 0;
}
// Ожидание событий epoll
int Epoll::wait(epoll_event *events, int maxevents, int timeout) {
    return epoll_wait(epfd, events, maxevents, timeout);
}
#endif