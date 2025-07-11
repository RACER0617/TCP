cmake_minimum_required(VERSION 3.10)
project(tcp_calc_epoll LANGUAGES CXX)
# Устанавливаем стандарт C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Библиотека-обёртка для epoll/WSAPoll
add_library(epoll_wrapper
        epoll_wrapper.cpp
        epoll_wrapper.h
)
# Указываем директорию для заголовков
target_include_directories(epoll_wrapper PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Выполняемый файл сервера
add_executable(calc_server server.cpp)
# Линкуем с обёрткой
target_link_libraries(calc_server PRIVATE epoll_wrapper)

# Выполняемый файл клиента
add_executable(calc_client client.cpp)
target_link_libraries(calc_client PRIVATE epoll_wrapper)

# На Windows нужно подключить библиотеку Winsock
if (WIN32)
    target_link_libraries(calc_server PRIVATE ws2_32)
    target_link_libraries(calc_client PRIVATE ws2_32)
endif()