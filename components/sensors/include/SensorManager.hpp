#pragma once
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include "ISensor.hpp"
#include "ISubscription.hpp"

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
}

namespace alco::core {

class SensorManager : public ISubscription {
public:
    explicit SensorManager(const char* taskName = "sensor_mgr",
                           uint32_t periodMs = 1000,
                           uint32_t stack = 4096,
                           UBaseType_t prio = tskIDLE_PRIORITY+1);
    ~SensorManager();

    void add(std::unique_ptr<ISensor> sensor);
    void start();   // create task and start pooling
    void stop();    // stop task

    const std::vector<std::unique_ptr<ISensor>>& getSensors() const { return sensors_; }

    // ISubscription
    void subscribe(SensorCallback cb) override;
    void unsubscribeAll() override;

private:
    static void taskEntry(void* arg);
    void loop();

    std::vector<std::unique_ptr<ISensor>> sensors_;
    std::vector<SensorCallback> subscribers_;
    std::mutex mtx_;

    TaskHandle_t task_{nullptr};
    const char* taskName_;
    uint32_t periodMs_;
    uint32_t stack_;
    UBaseType_t prio_;
    std::atomic<bool> running_{false};
    SemaphoreHandle_t doneSem_{nullptr};
    size_t initCount_{0};          // How many sensors have already been initialized

};

} // namespace
