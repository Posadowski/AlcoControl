#include "heater_safety.hpp"
#include <cmath>

using namespace alco::heater;

const char* alco::heater::toString(Fault f) {
    switch (f) {
        case Fault::None:           return "none";
        case Fault::InvalidReading: return "invalid reading";
        case Fault::TooLow:         return "temperature too low";
        case Fault::Overtemp:       return "overtemperature";
        case Fault::SensorTimeout:  return "sensor timeout";
    }
    return "?";
}

void HeaterSafety::arm(uint64_t nowMs) {
    std::lock_guard<std::mutex> lk(m_);
    armed_ = true;
    hasReading_ = false;
    lastMs_ = nowMs;              // the timeout counts from arming
}

void HeaterSafety::disarm() {
    std::lock_guard<std::mutex> lk(m_);
    armed_ = false;
    hasReading_ = false;
}

void HeaterSafety::onTemperature(float c, uint64_t nowMs) {
    std::lock_guard<std::mutex> lk(m_);
    if (!armed_ || fault_ != Fault::None) return;

    if (!std::isfinite(c))          { fault_ = Fault::InvalidReading; return; }
    if (c > cfg_.maxValidC)         { fault_ = Fault::Overtemp;       return; }
    if (c < cfg_.minValidC)         { fault_ = Fault::TooLow;         return; }

    lastC_ = c;
    lastMs_ = nowMs;
    hasReading_ = true;
}

bool HeaterSafety::check(uint64_t nowMs) {
    std::lock_guard<std::mutex> lk(m_);
    if (fault_ != Fault::None) return false;
    if (!armed_) return false;
    if (nowMs > lastMs_ && (nowMs - lastMs_) > cfg_.timeoutMs) {
        fault_ = Fault::SensorTimeout;
        return false;
    }
    return hasReading_;           // no heating before the first valid reading
}

void HeaterSafety::clear() {
    std::lock_guard<std::mutex> lk(m_);
    fault_ = Fault::None;
    armed_ = false;
    hasReading_ = false;
}

Fault HeaterSafety::fault() const {
    std::lock_guard<std::mutex> lk(m_);
    return fault_;
}

float HeaterSafety::lastTemperature() const {
    std::lock_guard<std::mutex> lk(m_);
    return lastC_;
}