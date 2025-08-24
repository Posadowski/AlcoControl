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

    httpd_config_t config = {                        \
        .task_priority      = tskIDLE_PRIORITY+5,       \
        .stack_size         = 4096,                     \
        .core_id            = tskNO_AFFINITY,           \
        .server_port        = 80,                       \
        .ctrl_port          = ESP_HTTPD_DEF_CTRL_PORT,  \
        .max_open_sockets   = 7,                        \
        .max_uri_handlers   = 15,                        \
        .max_resp_headers   = 15,                        \
        .backlog_conn       = 5,                        \
        .lru_purge_enable   = false,                    \
        .recv_wait_timeout  = 5,                        \
        .send_wait_timeout  = 5,                        \
        .global_user_ctx = NULL,                        \
        .global_user_ctx_free_fn = NULL,                \
        .global_transport_ctx = NULL,                   \
        .global_transport_ctx_free_fn = NULL,           \
        .enable_so_linger = false,                      \
        .linger_timeout = 0,                            \
        .keep_alive_enable = false,                     \
        .keep_alive_idle = 0,                           \
        .keep_alive_interval = 0,                       \
        .keep_alive_count = 0,                          \
        .open_fn = NULL,                                \
        .close_fn = NULL,                               \
        .uri_match_fn = NULL                            \
};

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
