#include <iostream>
#include <exception>

#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

class http_exception_t: public std::exception {
    public:
    virtual const char* what() const noexcept{
        return exception_line.c_str();
    }
    explicit http_exception_t(const char* error_line): exception_line(error_line){}
    virtual ~http_exception_t() = default;
private:
    std::string exception_line;
};


class internal_proxy_exception_t: public std::exception {
    public:
    virtual const char* what() const noexcept{
        return exception_line.c_str();
    }
    explicit internal_proxy_exception_t(const char* error_line): exception_line(error_line){}
    virtual ~internal_proxy_exception_t() = default;
private:
    std::string exception_line;
};


#endif