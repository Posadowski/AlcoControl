#pragma once
#include <functional>
#include <map>
#include <string>

namespace alco::web {

enum class Method { Get, Post, Put, Delete, Other };

struct Request {
    Method method{Method::Get};
    std::string path;                               // without the query string
    std::string query;                              // raw query string, without '?'
    std::string body;
    std::map<std::string, std::string> params;      // query + form-encoded body (body wins)

    const std::string* param(const std::string& name) const {
        auto it = params.find(name);
        return it == params.end() ? nullptr : &it->second;
    }
};

struct Response {
    int status{200};
    std::string contentType{"text/html; charset=utf-8"};
    std::string body;

    static Response html(std::string body, int status = 200);
    static Response json(std::string body, int status = 200);
    static Response error(int status, const std::string& message);   // {"error":"..."}
};

using Handler = std::function<Response(const Request&)>;

} // namespace alco::web
