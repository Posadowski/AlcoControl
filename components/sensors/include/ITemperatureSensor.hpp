#pragma once
#include "ISensor.hpp"

namespace alco::core {

class ITemperatureSensor : public ISensor {
public:
    ~ITemperatureSensor() override = default;

    // return temperature in °C
    virtual Status readCelsius(float& out) = 0;
};

} // namespace
