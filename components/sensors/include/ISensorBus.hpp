#pragma once
#include <string>
#include <vector>
#include "Status.hpp"

namespace alco::core {

class ISensorBus {
public:
    virtual ~ISensorBus() = default;

    virtual Status init() = 0;
    virtual std::string name() const = 0;
};

} // namespace
