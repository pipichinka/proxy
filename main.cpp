#include <stdio.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <stdlib.h>
#include <memory.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <iostream>
#include "thread_pool.hpp"

#define ERROR -1
#define NUM_THREADS 2



int main(int argc, char** argv){
    if (argc < 2){
        std::cout << "proxy usage:\n./proxy <port to listen connections> <num_threads>\n";
        return 0;
    }

    init_global_storage();

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1){
        perror("create socket");
        return ERROR;
    }
    struct sockaddr_in listen_addr;
    memset(&listen_addr, 0x0, sizeof(listen_addr));
    int port = std::stoi(argv[1]);
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    listen_addr.sin_port = htons(port);
    if (bind(sock, (struct sockaddr*) &listen_addr, sizeof(listen_addr))){
        perror("bind socket");
        close(sock);
        return ERROR;
    }
    
    if (listen(sock, 5)){
        perror("make socket listen");
        close(sock);
        return -1;
    }


    size_t num_threads = NUM_THREADS;
    if (argc == 3){
        num_threads = std::stoul(argv[2]);
    }

    thread_pool_t thread_pool(num_threads);


    for(;;){
        /*
         * Если ты всё равно адрес не используешь для определения источника соединения,
         * его можно не указывать, передав просто NULL
         * */
        struct sockaddr_in addr;
        unsigned int addr_len = sizeof(addr);
        int sock_fd = accept(sock, (struct sockaddr*) &addr, &addr_len);
        if (sock_fd == -1){
            perror("accept new connection");
            abort();
        }
        thread_pool.add_new_connection(sock_fd);
    }
}
