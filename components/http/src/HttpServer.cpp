#include "HttpServer.hpp"
#include "esp_log.h"
#include "http_util.hpp"

using alco::web::Method;
using alco::web::Request;
using alco::web::Response;

static const char* TAG = "HttpServer";
static constexpr size_t kMaxBody = 2048;       // forms only; larger bodies are rejected
static constexpr int kMaxRecvRetries = 3;

static Method toMethod(int m) {
    switch (m) {
        case HTTP_GET:    return Method::Get;
        case HTTP_POST:   return Method::Post;
        case HTTP_PUT:    return Method::Put;
        case HTTP_DELETE: return Method::Delete;
        default:          return Method::Other;
    }
}

static void sendResponse(httpd_req_t* req, const Response& res) {
    // httpd keeps these pointers until the response is sent: a static status line and
    // strings owned by `res` (which outlives this call) are safe.
    httpd_resp_set_status(req, alco::web::statusLine(res.status));
    httpd_resp_set_type(req, res.contentType.c_str());
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_send(req, res.body.data(), static_cast<ssize_t>(res.body.size()));
}

HttpServer& HttpServer::instance() {
    static HttpServer inst;
    return inst;
}

esp_err_t HttpServer::onRequest(httpd_req_t* req) {
    return static_cast<HttpServer*>(req->user_ctx)->handle(req);
}

esp_err_t HttpServer::handle(httpd_req_t* req) {
    Request r;
    r.method = toMethod(req->method);
    alco::web::splitTarget(req->uri, r.path, r.query);

    if (req->content_len > kMaxBody) {
        sendResponse(req, Response::error(413, "request body too large"));
        return ESP_FAIL;                                   // close the connection, the body is unread
    }
    if (req->content_len > 0) {
        r.body.resize(req->content_len);
        size_t got = 0;
        int retries = 0;
        while (got < req->content_len) {
            const int n = httpd_req_recv(req, &r.body[got], req->content_len - got);
            if (n == HTTPD_SOCK_ERR_TIMEOUT) {
                if (++retries > kMaxRecvRetries) {
                    httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, nullptr);
                    return ESP_FAIL;
                }
                continue;
            }
            if (n <= 0) return ESP_FAIL;
            got += static_cast<size_t>(n);
        }
    }

    r.params = alco::web::parseForm(r.query);
    for (const auto& kv : alco::web::parseForm(r.body)) r.params[kv.first] = kv.second;

    sendResponse(req, router_.dispatch(r));
    return ESP_OK;
}

esp_err_t HttpServer::start() {
    if (server_) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;                              // handlers build strings and scan WiFi
    config.max_uri_handlers = 4;                           // only the two wildcard handlers
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;                        // free the oldest socket instead of refusing new ones
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;

    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %d", static_cast<int>(err));
        server_ = nullptr;
        return err;
    }

    const httpd_method_t methods[] = {HTTP_GET, HTTP_POST};
    for (const httpd_method_t m : methods) {
        httpd_uri_t uri = {};
        uri.uri = "/*";
        uri.method = m;
        uri.handler = &HttpServer::onRequest;
        uri.user_ctx = this;
        err = httpd_register_uri_handler(server_, &uri);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "register handler failed: %d", static_cast<int>(err));
            httpd_stop(server_);
            server_ = nullptr;
            return err;
        }
    }
    return ESP_OK;
}

void HttpServer::stop() {
    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
    }
}
