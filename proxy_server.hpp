#include <iostream>
#include <vector>
#include <memory>
#include "selector_context.hpp"
#include "storage.hpp"


#ifndef PROXY_SERVER_H
#define PROXY_SERVER_H


#define MAX_ONE_TIME_READ 1024 * 64
#define MAX_ONE_TIME_WRITE 1024 * 64

class proxy_server_t;


class connection_t{
public:
    connection_t() = default;

    virtual void process_input( [[maybe_unused]] proxy_server_t& server) = 0;

    virtual void process_output( [[maybe_unused]] proxy_server_t& server) = 0;

    virtual ~connection_t() = default;
};


enum client_stages {
    CL_READ_FIRST_LINE,
    CL_READ_HOST,
    CL_READ_TILL_END,
    CL_SEND_ANSWER
};



class client_connection_t: public connection_t {
    int fd;
    
    std::string request;

    std::string host;

    std::string url;

    std::shared_ptr<item_t> storage_item;

    size_t last_unparsed_line_start;

    enum client_stages stage;

    size_t send_offset;

    void change_to_write_stage(proxy_server_t& server);
public:
    client_connection_t(int fd);

    virtual void process_input( [[maybe_unused]] proxy_server_t& server);

    virtual void process_output( [[maybe_unused]] proxy_server_t& server);

    virtual ~client_connection_t();
};


enum server_stages {
    SV_CONNECT, 
    SV_SEND_REQUEST, 
    SV_READ_FIRST_LINE,
    SV_READ_HEADERS, 
    SV_READ_TILL_END 
};



class server_connection_t: public connection_t {
    int fd;
    
    std::string request_to_send;

    size_t request_offset;

    std::pair<std::string, std::shared_ptr<item_t>> storage_item;

    ssize_t content_len; // -1 is untill server closes connection

    size_t content_offset;

    std::string tmp_answer_buffer; // used to parse first line of answer

    std::string host;

    size_t last_unparsed_line_start;

    int http_code;

    enum server_stages stage;
public:
    server_connection_t(std::string&& host, std::string&& request, std::pair<std::string, std::shared_ptr<item_t>>& storage_item, proxy_server_t& server);

    virtual void process_input( [[maybe_unused]] proxy_server_t& server);

    virtual void process_output( [[maybe_unused]] proxy_server_t& server);

    virtual ~server_connection_t();

    int get_fd() { return fd;}
};


class proxy_server_t{
    std::map<int, connection_t*> connections;
    selector_context_t selector_context;
public:
    explicit proxy_server_t();

    //thread_safe
    void add_client_socket(int fd);

    //thread_safe
    void add_server_socket(int fd);

    void change_cliend_sock_mod(int fd, uint32_t op);
    void start_server_loop();

    selector_context_t* get_selector_ptr() { return &selector_context;}

    void add_new_connection(int fd, connection_t* con);
};



#endif