#include "SensorManager.hpp"
#include "Reading.hpp"
#include <cstdio>

using namespace alco::core;

SensorManager::SensorManager(const char* taskName, uint32_t periodMs,
                             uint32_t stack, UBaseType_t prio)
: taskName_(taskName), periodMs_(periodMs), stack_(stack), prio_(prio) {}

SensorManager::~SensorManager() {
    stop();
    if (doneSem_) vSemaphoreDelete(doneSem_);
}

void SensorManager::add(std::unique_ptr<ISensor> sensor) {
    std::lock_guard<std::mutex> lk(mtx_);
    sensors_.push_back(std::move(sensor));
}

void SensorManager::start() {
    if (running_.exchange(true)) return;
    if (!doneSem_) doneSem_ = xSemaphoreCreateBinary();
    xTaskCreate(&SensorManager::taskEntry, taskName_, stack_, this, prio_, &task_);
}

void SensorManager::stop() {
    if (!running_.exchange(false)) return;
    xSemaphoreTake(doneSem_, portMAX_DELAY);
    task_ = nullptr;
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
    auto* self = static_cast<SensorManager*>(arg);
    self->loop();
    xSemaphoreGive(self->doneSem_);
    vTaskDelete(nullptr);
}

void SensorManager::loop() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        initCount_ = 0;                       // init at every startup
    }

    while (running_) {
        {
            std::lock_guard<std::mutex> lk(mtx_);

            // also initialize sensors added after start()
            for (; initCount_ < sensors_.size(); ++initCount_) {
                auto& s = sensors_[initCount_];
                auto st = s->init();
                if (!st) {
                    printf("[SensorManager] init failed for %s: %s\n",
                           s->id().c_str(), st.message.c_str());
                }
            }

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
