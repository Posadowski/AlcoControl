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
    void start();   // create task and start polling
    void stop();    // stop task (safe to call from a subscriber callback)

    // Snapshot of the sensor list; safe to use concurrently with add().
    std::vector<std::shared_ptr<ISensor>> getSensors() const;

    // ISubscription
    void subscribe(SensorCallback cb) override;
    void unsubscribeAll() override;

private:
    static void taskEntry(void* arg);
    void loop();

    std::vector<std::shared_ptr<ISensor>> sensors_;   // append-only
    std::vector<SensorCallback> subscribers_;
    mutable std::mutex mtx_;                          // protects both vectors

    TaskHandle_t task_{nullptr};
    const char* taskName_;
    uint32_t periodMs_;
    uint32_t stack_;
    UBaseType_t prio_;
    std::atomic<bool> running_{false};
    std::atomic<bool> selfStopped_{false};            // stop() called from the task itself
    SemaphoreHandle_t doneSem_{nullptr};
    size_t initCount_{0};                             // touched only by the task
};

} // namespace