#include "proxy_server.hpp"
#include <unistd.h>
#include <string.h>
#include <netdb.h>
proxy_server_t::proxy_server_t() {

}


void proxy_server_t::start_server_loop(){
    while (true){
        int n = selector_context.do_select();


        for (int i = 0; i < n; ++i){
            int fd = selector_context[i].data.fd;
            uint32_t event = selector_context[i].events;
            auto it = connections.find(fd);
            connection_t* connection = nullptr;
            if (it == connections.end()){
                connection = new client_connection_t(fd);
                auto res = connections.emplace(fd, connection);
                if (!res.second){
                    selector_context.unregister_file_descriptor(fd);
                    close(fd);
                    delete connection;
                    continue;
                }
            }
            else{
                connection = it->second;
            }
            try {
                if (event & (EPOLLERR|EPOLLHUP)) {
                    std::cout << "connection close event " << event << "\n";
                    connections.erase(fd);
                    selector_context.unregister_file_descriptor(fd);
                    delete connection;
                    continue;
                }
                if (event & READ){
                    connection->process_input(*this);
                }
                if (event & WRITE){
                    connection->process_output(*this);
                }
            }
            catch(const std::exception& e){
                std::cout << e.what() << "\n";
                connections.erase(fd);
                selector_context.unregister_file_descriptor(fd);
                delete connection;
            }
            

        }
    }
}


void proxy_server_t::add_client_socket(int fd){
    selector_context.register_file_descriptor(fd, READ);
}


void proxy_server_t::change_cliend_sock_mod(int fd, uint32_t op){
    selector_context.change_descriptor_mode(fd, op);
}


void proxy_server_t::add_server_socket(int fd){
    selector_context.register_file_descriptor(fd, WRITE);
}


void proxy_server_t::add_new_connection(int fd, connection_t* con){
    auto res = connections.emplace(fd, con);
    if (!res.second){
        delete con;
        throw std::runtime_error("can't add new connection");
    }
}


client_connection_t::client_connection_t(int fd): fd(fd), last_unparsed_line_start(0), stage(client_stages::CL_READ_FIRST_LINE), send_offset(0){

}


void change_http_version_in_message(std::string& request, size_t http_ver_index, size_t http_ver_size){
    char target_http_ver[] = "HTTP/1.0";
    for (size_t i = 0; i < http_ver_size; ++i){
        request[i + http_ver_index] = target_http_ver[i];
    }
    if (http_ver_size < sizeof(target_http_ver) - 1){
        request.insert(http_ver_size + http_ver_index, target_http_ver + http_ver_size, sizeof(target_http_ver) - 1 - http_ver_size);
    }
}


void client_connection_t::change_to_write_stage(proxy_server_t& server){
    auto item = storage->get_item(host + url);
    if (!item.second->is_started()){
        bool res = item.second->set_started(true);
        if (!res){
            server_connection_t* server_connection = new server_connection_t(std::move(host), std::move(request), item, server);
            server.add_new_connection(server_connection->get_fd(), server_connection);
        }
    }
    storage_item = item.second;
    server.change_cliend_sock_mod(fd, WRITE);
    stage = client_stages::CL_SEND_ANSWER;
}



void client_connection_t::process_input( [[maybe_unused]] proxy_server_t& server){
    if (stage == client_stages::CL_SEND_ANSWER){
        return;
    }

    /*
     * А ты уверен, что read() не заблокируется с таким размером буфера?
     *
     * Если да, жду пруф
     * */
    char read_buffer[MAX_ONE_TIME_READ];
    int res = read(fd, read_buffer, MAX_ONE_TIME_READ);
    if (res == 0){
        throw std::runtime_error("client closed socket");
    }
    else if (res == -1){
        /*
         * А откуда EWOULDBLOCK? Ты же не делаешь сокет неблокирующимся?
         * */
        if (errno == EAGAIN || errno == EWOULDBLOCK){
            return;
        }
        throw std::runtime_error(strerror(errno));
    }

    request.append(read_buffer, res);
    if (stage == client_stages::CL_READ_FIRST_LINE){
        size_t end_line_pos = request.find("\r\n");
        if (end_line_pos == std::string::npos){
            return;
        }

        if (request.substr(0, 3) != "GET"){
            throw std::runtime_error("unsuportable protocol operation");
        }
        size_t last_space_pos = request.find_last_of(" ", end_line_pos);
        if (last_space_pos == std::string::npos){
            throw std::runtime_error("invalid GET request header");
        }
        change_http_version_in_message(request, last_space_pos + 1, end_line_pos - 1 - last_space_pos);
        url = request.substr(4, last_space_pos - 4);
        stage = client_stages::CL_READ_HOST;
        last_unparsed_line_start = end_line_pos + 2;
    }

    if (stage == client_stages::CL_READ_HOST){
        while(last_unparsed_line_start < request.length()){
            size_t end_line_pos = request.find("\r\n", last_unparsed_line_start);
            if (end_line_pos == std::string::npos){
                return;
            }

            if (last_unparsed_line_start == end_line_pos){
                change_to_write_stage(server);
                return;
            }

            std::string line = request.substr(last_unparsed_line_start, end_line_pos - last_unparsed_line_start);
            size_t param_end_index = line.find(':');
            if (param_end_index == std::string::npos){
                throw std::runtime_error("invalid parametr in HTTP header");
            }

            if (line.substr(0, param_end_index) == "Host"){
                host = line.substr(param_end_index + 2);
                last_unparsed_line_start = end_line_pos + 2;
                stage = client_stages::CL_READ_TILL_END;
                break;
            }
        }
    }
    if (stage == client_stages::CL_READ_TILL_END){
        size_t end_pos = request.find_last_of("\r\n\r\n");
        if (end_pos != std::string::npos){
            change_to_write_stage(server);
            return;
        }
    }
}  



void client_connection_t::process_output([[maybe_unused]] proxy_server_t& server){
    if (stage != client_stages::CL_SEND_ANSWER){
        return;
    }

    std::string buffer;
    
    int res = storage_item->get_data(buffer, send_offset, MAX_ONE_TIME_WRITE, wait_context_t(fd, server.get_selector_ptr()));
    if (res == -1){
        throw std::runtime_error("sended all answer");
    }

    if (res == 0){ // we wait untill data arive in storage_item
        return;
    }

    ssize_t send_res = write(fd, buffer.c_str(), buffer.length());
    if (send_res == -1){
        if (errno == EAGAIN){
            return;
        }
        throw std::runtime_error(strerror(errno));
    }
    if (send_res == 0){
        throw std::runtime_error("client closed socket");
    }

    send_offset += static_cast<size_t>(send_res);
}


client_connection_t::~client_connection_t(){
    if (storage_item){
        storage_item->un_pin(fd);
    }
    close(fd);
}



server_connection_t::server_connection_t(std::string&& host, std::string&& request, std::pair<std::string, std::shared_ptr<item_t>>& storage_item, proxy_server_t& server):
    request_to_send(request), request_offset(0), storage_item(storage_item), content_len(-1), content_offset(0),  host(host), http_code(0), stage(SV_CONNECT) {
    

    struct addrinfo hints;
    struct addrinfo *result;
    int s;
    char service[] = "http";
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;   
    hints.ai_socktype = SOCK_STREAM; 
    hints.ai_flags = 0;
    hints.ai_protocol = 0;
    /*
     * А нет, всё же ты делаешь их неблокирующими.
     *
     * Так не делаем тут, сокеты должны быть блокирующими,
     * но обрабатываться так, чтобы никогда не блокироваться на операциях чтения/записи
     * */
    fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);


    s = getaddrinfo(this->host.c_str(), service, &hints, &result);
    if (s != 0) {
        throw std::runtime_error(strerror(errno));
    }

    int res = connect(fd, result->ai_addr, result->ai_addrlen);

    if (res == 0){
        stage = SV_SEND_REQUEST;
        server.add_server_socket(fd);
        return;
    }
    if (errno == EINPROGRESS){
        server.add_server_socket(fd);
        return;
    }
    else {
        close(fd);
    }
    freeaddrinfo(result);
}


server_connection_t::~server_connection_t(){
    close(fd);
    storage_item.second->set_complited(true);
    if (http_code != 200){
        storage->remove_item(storage_item.first);
    }
}


void server_connection_t::process_output(proxy_server_t& server){
    if (stage == SV_CONNECT){
        char tmp_buffer[1];
        ssize_t res = read(fd, tmp_buffer, 0); // checking if connection complited
        if (res == 0){
            stage = SV_SEND_REQUEST;
        }
        else {
            throw std::runtime_error("can't connect to server");
        }
    }

    if (stage != SV_SEND_REQUEST){
        return;
    }

    ssize_t res = write(fd, request_to_send.c_str() + request_offset, 
                        std::min(request_to_send.length() - request_offset, (unsigned long) MAX_ONE_TIME_WRITE));

    if (res == 0){
        throw std::runtime_error("connection is closed by the server");
    }
    if (res == -1){
        if (errno == EAGAIN){
            return;
        }
        throw std::runtime_error(strerror(errno));
    }

    request_offset += res;

    if (request_offset == request_to_send.length()){
        request_to_send.clear();
        stage = server_stages::SV_READ_FIRST_LINE;
        server.change_cliend_sock_mod(fd, READ);
    }
}


void server_connection_t::process_input( [[maybe_unused]] proxy_server_t& server){
    

    if (stage != server_stages::SV_READ_FIRST_LINE && stage != server_stages::SV_READ_TILL_END && stage != server_stages::SV_READ_HEADERS){
        return;
    }

    char read_buffer[MAX_ONE_TIME_READ];

    ssize_t res = read(fd, read_buffer, MAX_ONE_TIME_READ);
    if (res == 0){
        if (stage != server_stages::SV_READ_TILL_END){
            storage_item.second->put_data(tmp_answer_buffer);
        }
        throw std::runtime_error("connection is closed by the server");
    }
    if (res == -1){
        if (errno == EAGAIN){
            return;
        }

        if (stage == server_stages::SV_READ_FIRST_LINE || stage == server_stages::SV_READ_HEADERS){
            storage_item.second->put_data(tmp_answer_buffer);
        }
        throw std::runtime_error(strerror(errno));
    }

    if (stage == server_stages::SV_READ_FIRST_LINE || stage == server_stages::SV_READ_HEADERS){
        tmp_answer_buffer.append(read_buffer, res);
    }

    if (stage == server_stages::SV_READ_FIRST_LINE){
        size_t end_line_pos = tmp_answer_buffer.find("\r\n");
        if (end_line_pos == std::string::npos){
            return;
        }

        size_t first_space_pos = tmp_answer_buffer.find(" ");
        
        if (first_space_pos == std::string::npos || first_space_pos > end_line_pos){
            throw std::runtime_error("invalid answer format");
        }

        std::string code_string = tmp_answer_buffer.substr(first_space_pos + 1, 3);

        http_code = std::stoi(code_string);
        size_t size_before_change = tmp_answer_buffer.length();
        change_http_version_in_message(tmp_answer_buffer, 0, first_space_pos);
        size_t size_after_change = tmp_answer_buffer.length();
        stage = server_stages::SV_READ_HEADERS;
        last_unparsed_line_start = end_line_pos + 2 + (size_after_change - size_before_change);
    }

    if (stage == server_stages::SV_READ_HEADERS){
        while(last_unparsed_line_start < tmp_answer_buffer.length()){

            size_t end_line_pos = tmp_answer_buffer.find("\r\n", last_unparsed_line_start);
            if (end_line_pos == std::string::npos){
                return;
            }

            if (last_unparsed_line_start == end_line_pos){
                stage = server_stages::SV_READ_TILL_END; 
                storage_item.second->put_data(tmp_answer_buffer);
                content_offset = tmp_answer_buffer.length() - (end_line_pos + 3);
                tmp_answer_buffer.clear();
                return;
            }

            std::string line = tmp_answer_buffer.substr(last_unparsed_line_start, end_line_pos - last_unparsed_line_start);
            size_t param_end_index = line.find(':');
            if (param_end_index == std::string::npos){
                throw std::runtime_error("invalid parametr in HTTP header");
            }

            if (line.substr(0, param_end_index) == "Content-Length"){
                content_len = std::stol(line.substr(param_end_index + 1));
            }
            last_unparsed_line_start = end_line_pos + 2;
        }
    }


    if (stage == server_stages::SV_READ_TILL_END){
        size_t read_count = res;
        if (content_len > 0){
            read_count = std::min(read_count, static_cast<size_t> (content_len) - content_offset);
        }
        std::string current_data(read_buffer, read_count);

        content_offset += read_count;
        
        storage_item.second->put_data(current_data);

        if (static_cast<ssize_t> (content_offset) == content_len){
            storage_item.second->set_complited(true);
            throw std::runtime_error("server connection finished downloading data");
        }
    }
}