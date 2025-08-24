#pragma once
#include <functional>
#include <string>
#include "Reading.hpp"
#include "Status.hpp"

namespace alco::core {

using SensorCallback = std::function<void(const std::string& sensorId, const Reading&)>;

class ISubscription {
public:
    virtual ~ISubscription() = default;
    virtual void subscribe(SensorCallback cb) = 0;
    virtual void unsubscribeAll() = 0;
};

} // namespace
