#pragma once
#include <mutex>
#include <string>
#include <vector>
#include "http_types.hpp"

namespace alco::web {

// Maps (method, path) to a handler. Pure logic: no sockets, testable without a network.
//  - the path is matched exactly; a trailing '/' is ignored ("/ota/" == "/ota")
//  - a known path with another method gives 405, an unknown path gives 404
//  - adding the same (method, path) again replaces the handler
class Router {
public:
    void add(Method method, std::string path, Handler handler);
    Response dispatch(const Request& req) const;
    size_t size() const;

private:
    struct Route {
        Method method;
        std::string path;
        Handler handler;
    };
    static std::string normalize(const std::string& path);

    mutable std::mutex mtx_;
    std::vector<Route> routes_;
};

} // namespace alco::web
