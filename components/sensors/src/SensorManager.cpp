#include "SensorManager.hpp"
#include "Reading.hpp"
#include <cstdio>

using namespace alco::core;

SensorManager::SensorManager(const char* taskName, uint32_t periodMs,
                             uint32_t stack, UBaseType_t prio)
: taskName_(taskName), periodMs_(periodMs), stack_(stack), prio_(prio) {}

SensorManager::~SensorManager() {
    stop();
}

void SensorManager::add(std::unique_ptr<ISensor> sensor) {
    std::lock_guard<std::mutex> lk(mtx_);
    sensors_.push_back(std::move(sensor));
}

void SensorManager::start() {
    if (running_) return;
    running_ = true;
    xTaskCreate(&SensorManager::taskEntry, taskName_, stack_, this, prio_, &task_);
}

void SensorManager::stop() {
    if (!running_) return;
    running_ = false;
    if (task_) {
        vTaskDelete(task_);
        task_ = nullptr;
    }
}

void SensorManager::subscribe(SensorCallback cb) {
    std::lock_guard<std::mutex> lk(mtx_);
    subscribers_.push_back(std::move(cb));
}

void SensorManager::unsubscribeAll() {
    std::lock_guard<std::mutex> lk(mtx_);
    subscribers_.clear();
}

void SensorManager::taskEntry(void* arg) {
    static_cast<SensorManager*>(arg)->loop();
    vTaskDelete(nullptr);
}

void SensorManager::loop() {
    // 1) int for all sensors
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto& s : sensors_) {
            auto st = s->init();
            if (!st) {
                printf("[SensorManager] init failed for %s: %s\n",
                    s->id().c_str(), st.message.c_str());
            }
        }
    }

    // 2) sensor main loop
    while (running_) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            for (auto& s : sensors_) {
                Reading r{};
                auto st = s->read(r);
                if (st) {
                    for (auto& cb : subscribers_) cb(s->id(), r);
                } else {
                    printf("[SensorManager] read failed for %s: %s\n",
                           s->id().c_str(), st.message.c_str());
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(periodMs_));
    }
}
