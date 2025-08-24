#pragma once
#include "esp_http_server.h"
#include <functional>
#include <string>
#include <map>

class HttpServer {
public:
    static HttpServer& instance();

    void start();
    void stop();
    void registerEndpoint(const std::string& uri,
                          httpd_method_t method,
                          std::function<std::string()> handler);
    std::map<std::string, std::function<std::string()>> handlers_;
    
private:
    HttpServer() = default;
    httpd_handle_t server_ = nullptr;
};
