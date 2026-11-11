#include "router.hpp"

namespace alco::web {

std::string Router::normalize(const std::string& path) {
    std::string p = path.empty() ? "/" : path;
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

void Router::add(Method method, std::string path, Handler handler) {
    path = normalize(path);
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& r : routes_) {
        if (r.method == method && r.path == path) {
            r.handler = std::move(handler);
            return;
        }
    }
    routes_.push_back({method, std::move(path), std::move(handler)});
}

size_t Router::size() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return routes_.size();
}

Response Router::dispatch(const Request& req) const {
    const std::string path = normalize(req.path);
    Handler handler;
    bool pathKnown = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (const auto& r : routes_) {
            if (r.path != path) continue;
            pathKnown = true;
            if (r.method == req.method) {
                handler = r.handler;                       // copy; call it without holding the lock
                break;
            }
        }
    }
    if (handler) return handler(req);
    if (pathKnown) return Response::error(405, "method not allowed");
    return Response::error(404, "not found");
}

} // namespace alco::web
