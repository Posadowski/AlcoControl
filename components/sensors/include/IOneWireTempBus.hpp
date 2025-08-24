#pragma once
#include <string>
#include <vector>
#include "ISensorBus.hpp"
#include "Status.hpp"

namespace alco::core {

struct OneWireRom {
    std::string rom; // for example "28-abcdef012345"
};

class IOneWireTempBus : public ISensorBus {
public:
    ~IOneWireTempBus() override = default;

    virtual Status search(std::vector<OneWireRom>& out) = 0;
    virtual Status startConversion(const OneWireRom& dev) = 0;
    virtual Status readTemperature(const OneWireRom& dev, float& outCelsius) = 0;
};

} // namespace
