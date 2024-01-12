#include <unistd.h>
#include <errno.h>
#include <iostream>
#include "proxy_server.hpp"

/*
 * В C++ обычно используют CamelCase для именования сущностей.
 * Названия классов с большой буквы,
 * методов с маленькой (хотя, их иногда пишут с большой, по аналогии с C#),
 * переменных с маленькой.
 *
 * Как минимум, у тебя вызов конструктора легко путается с вызовом обычной функции,
 * в т.ч. из-за того, что никто не ожидает такое название класса
 * */
class thread_pool_t{
    size_t num_threads;
    size_t next;

    pthread_t* thread_ids;

    proxy_server_t* proxy_servers;




public:
    explicit thread_pool_t(size_t num_threads);    

    void add_new_connection(int fd);


    ~thread_pool_t();
};