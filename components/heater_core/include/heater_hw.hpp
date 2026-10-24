#pragma once

namespace alco::heater {

class ITriac {
public:
    virtual ~ITriac() = default;
    // Request a gate pulse for the current half-cycle. Must be ISR-safe and non-blocking.
    virtual void fire() = 0;
    // Force the gate low.
    virtual void off() = 0;
};

} // namespace alco::heater