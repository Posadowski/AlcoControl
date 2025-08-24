#pragma once
#include <string>
#include "Status.hpp"
#include "Reading.hpp"

namespace alco::core {

class ISensor {
public:
    virtual ~ISensor() = default;

    virtual std::string id() const = 0;           // np. ROM code, adres I2C, alias
    virtual Status init() = 0;
    virtual Status read(Reading& out) = 0;        // blokujące/krótkie czytanie
};

} // namespace
