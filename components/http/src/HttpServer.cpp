#include "HttpServer.hpp"
#include "esp_log.h"

static const char* TAG = "HttpServer";

HttpServer& HttpServer::instance() {
    static HttpServer inst;
    return inst;
}

esp_err_t dispatch_handler(httpd_req_t* req) {
    auto* server = &HttpServer::instance();
    std::string uri(req->uri);

    auto it = server->handlers_.find(uri);
    if (it != server->handlers_.end()) {
        std::string resp = it->second();
        httpd_resp_send(req, resp.c_str(), resp.size());
        return ESP_OK;
    }
    httpd_resp_send_404(req);
    return ESP_FAIL;
}

void HttpServer::start() {
    if (server_) return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(httpd_start(&server_, &config));

    ESP_LOGI(TAG, "HTTP server started");
}

void HttpServer::stop() {
    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
    }
}

void HttpServer::registerEndpoint(const std::string& uri,
                                  httpd_method_t method,
                                  std::function<std::string()> handler) {
    handlers_[uri] = handler;
    httpd_uri_t endpoint = {
        .uri = uri.c_str(),
        .method = method,
        .handler = dispatch_handler,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(server_, &endpoint);
    ESP_LOGI(TAG, "Registered endpoint: %s", uri.c_str());
}
