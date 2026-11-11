#pragma once
#include "esp_err.h"
#include "esp_http_server.h"
#include "router.hpp"

// Thin adapter between esp_http_server and alco::web::Router.
// The server registers ONE wildcard handler per method and routes on its own, so the number of
// routes is not limited by max_uri_handlers (the default is 8; the old code silently lost the
// routes after the 8th one).
class HttpServer {
public:
    static HttpServer& instance();

    alco::web::Router& router() { return router_; }

    // Add the routes first, then call start().
    esp_err_t start();
    void stop();
    bool running() const { return server_ != nullptr; }

private:
    HttpServer() = default;
    static esp_err_t onRequest(httpd_req_t* req);
    esp_err_t handle(httpd_req_t* req);

    httpd_handle_t server_{nullptr};
    alco::web::Router router_;
};
