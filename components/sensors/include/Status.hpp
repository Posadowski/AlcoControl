#pragma once
#include <string>

namespace alco::core {

enum class StatusCode {
    Ok,
    NotReady,
    Timeout,
    NotFound,
    BusError,
    InvalidState,
    Internal
};

struct Status {
    StatusCode code{StatusCode::Ok};
    std::string message;

    static Status ok() { return {StatusCode::Ok, {}}; }
    explicit operator bool() const { return code == StatusCode::Ok; }
};

} // namespace
