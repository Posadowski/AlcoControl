#include "SensorManager.hpp"
#include "Reading.hpp"
#include <cstdio>

using namespace alco::core;

SensorManager::SensorManager(const char* taskName, uint32_t periodMs,
                             uint32_t stack, UBaseType_t prio)
: taskName_(taskName), periodMs_(periodMs), stack_(stack), prio_(prio) {}

SensorManager::~SensorManager() {
    stop();
    if (selfStopped_ && doneSem_) {
        xSemaphoreTake(doneSem_, portMAX_DELAY);   // task stopped itself; wait for it to exit
    }
    if (doneSem_) vSemaphoreDelete(doneSem_);
}

void SensorManager::add(std::unique_ptr<ISensor> sensor) {
    std::lock_guard<std::mutex> lk(mtx_);
    sensors_.push_back(std::move(sensor));
}

std::vector<std::shared_ptr<ISensor>> SensorManager::getSensors() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return sensors_;
}

void SensorManager::start() {
    if (selfStopped_.exchange(false)) {
        // previous task stopped itself from a callback: wait until it is really gone
        xSemaphoreTake(doneSem_, portMAX_DELAY);
        task_ = nullptr;
    }
    if (running_.exchange(true)) return;
    if (!doneSem_) doneSem_ = xSemaphoreCreateBinary();
    xTaskCreate(&SensorManager::taskEntry, taskName_, stack_, this, prio_, &task_);
}

void SensorManager::stop() {
    if (!running_.exchange(false)) return;
    if (xTaskGetCurrentTaskHandle() == task_) {
        // called from a subscriber callback: the task cannot wait for itself
        selfStopped_ = true;
        return;
    }
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
    initCount_ = 0;                                   // init at every start

    while (running_) {
        // Under the lock only copy; sensors and callbacks run without it.
        std::vector<std::shared_ptr<ISensor>> sensors;
        std::vector<SensorCallback> subscribers;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            sensors = sensors_;
            subscribers = subscribers_;
        }

        // also initialise sensors added after start()
        for (; initCount_ < sensors.size(); ++initCount_) {
            auto& s = sensors[initCount_];
            auto st = s->init();
            if (!st) {
                printf("[SensorManager] init failed for %s: %s\n",
                       s->id().c_str(), st.message.c_str());
            }
        }

        for (auto& s : sensors) {
            if (!running_) break;                     // faster stop
            Reading r{};
            auto st = s->read(r);
            if (st) {
                for (auto& cb : subscribers) cb(s->id(), r);
            } else {
                printf("[SensorManager] read failed for %s: %s\n",
                       s->id().c_str(), st.message.c_str());
            }
        }
        vTaskDelay(pdMS_TO_TICKS(periodMs_));
    }
}
