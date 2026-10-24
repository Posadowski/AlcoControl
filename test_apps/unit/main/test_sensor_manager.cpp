#include <atomic>
#include <memory>
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "SensorManager.hpp"
#include "mock_sensor.hpp"

using namespace alco::core;

namespace {
constexpr uint32_t kPeriodMs = 20;   // 2 ticks FREERTOS_HZ=100

void sleepMs(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

std::unique_ptr<SensorManager> makeMgr() {
    return std::make_unique<SensorManager>("t_mgr", kPeriodMs, 4096, tskIDLE_PRIORITY + 1);
}

struct Counters {
    std::atomic<int>   a{0};
    std::atomic<int>   b{0};
    std::atomic<float> lastA{0.f};
};

struct AddJob {
    SensorManager* mgr;
    std::shared_ptr<MockSensorState> st;
    SemaphoreHandle_t done;
};

void addTask(void* p) {
    auto* job = static_cast<AddJob*>(p);
    job->mgr->add(std::make_unique<MockSensor>("late", job->st));
    xSemaphoreGive(job->done);
    vTaskDelete(nullptr);
}
}

TEST_CASE("SensorManager: init once, then periodic reads", "[sensor_mgr]")
{
    auto st = std::make_shared<MockSensorState>();
    auto mgr = makeMgr();
    mgr->add(std::make_unique<MockSensor>("a", st));
    mgr->start();
    sleepMs(250);
    mgr->stop();
    TEST_ASSERT_EQUAL_INT(1, st->initCalls);
    TEST_ASSERT_TRUE(st->readCalls >= 3);
}

TEST_CASE("SensorManager: subscribers get sensor id and value", "[sensor_mgr]")
{
    auto sa = std::make_shared<MockSensorState>();
    auto sb = std::make_shared<MockSensorState>();
    sa->value = 11.0f;
    sb->value = 22.0f;
    Counters c;

    auto mgr = makeMgr();
    mgr->add(std::make_unique<MockSensor>("a", sa));
    mgr->add(std::make_unique<MockSensor>("b", sb));
    mgr->subscribe([&c](const std::string& id, const Reading& r) {
        if (id == "a") { ++c.a; c.lastA = r.value; }
        else if (id == "b") { ++c.b; }
    });
    mgr->start();
    sleepMs(200);
    mgr->stop();

    TEST_ASSERT_TRUE(c.a >= 2);
    TEST_ASSERT_TRUE(c.b >= 2);
    TEST_ASSERT_EQUAL_FLOAT(11.0f, c.lastA);
}

TEST_CASE("SensorManager: failed read does not notify subscribers", "[sensor_mgr][error]")
{
    auto st = std::make_shared<MockSensorState>();
    st->failRead = true;
    Counters c;

    auto mgr = makeMgr();
    mgr->add(std::make_unique<MockSensor>("a", st));
    mgr->subscribe([&c](const std::string&, const Reading&) { ++c.a; });
    mgr->start();
    sleepMs(150);
    mgr->stop();

    TEST_ASSERT_TRUE(st->readCalls >= 2);
    TEST_ASSERT_EQUAL_INT(0, c.a);
}

TEST_CASE("SensorManager: unsubscribeAll stops notifications", "[sensor_mgr]")
{
    auto st = std::make_shared<MockSensorState>();
    Counters c;

    auto mgr = makeMgr();
    mgr->add(std::make_unique<MockSensor>("a", st));
    mgr->subscribe([&c](const std::string&, const Reading&) { ++c.a; });
    mgr->start();
    sleepMs(120);
    mgr->unsubscribeAll();
    const int snapshot = c.a;
    sleepMs(120);
    mgr->stop();

    TEST_ASSERT_TRUE(snapshot >= 1);
    TEST_ASSERT_EQUAL_INT(snapshot, c.a);
}

TEST_CASE("SensorManager: stop halts reading", "[sensor_mgr]")
{
    auto st = std::make_shared<MockSensorState>();
    auto mgr = makeMgr();
    mgr->add(std::make_unique<MockSensor>("a", st));
    mgr->start();
    sleepMs(100);
    mgr->stop();
    const int snapshot = st->readCalls;
    sleepMs(120);
    TEST_ASSERT_EQUAL_INT(snapshot, st->readCalls);
}

TEST_CASE("SensorManager: can be restarted after stop", "[sensor_mgr]")
{
    auto st = std::make_shared<MockSensorState>();
    auto mgr = makeMgr();
    mgr->add(std::make_unique<MockSensor>("a", st));
    mgr->start();
    sleepMs(80);
    mgr->stop();
    const int afterFirst = st->readCalls;

    mgr->start();
    sleepMs(100);
    mgr->stop();

    TEST_ASSERT_TRUE(st->readCalls > afterFirst);
    TEST_ASSERT_EQUAL_INT(2, st->initCalls);   // init on every start
}

// ---------- code error tests ----------

TEST_CASE("SensorManager: sensor added after start() gets initialised", "[sensor_mgr][bug]")
{
    auto early = std::make_shared<MockSensorState>();
    auto late  = std::make_shared<MockSensorState>();

    auto mgr = makeMgr();
    mgr->add(std::make_unique<MockSensor>("early", early));
    mgr->start();
    sleepMs(60);
    mgr->add(std::make_unique<MockSensor>("late", late));
    sleepMs(100);
    mgr->stop();

    TEST_ASSERT_TRUE(late->readCalls >= 1);       // read...
    TEST_ASSERT_EQUAL_INT(1, late->initCalls);    // ...so it must be initiated
}

TEST_CASE("SensorManager: stop() during a slow read does not leave the lock held",
          "[sensor_mgr][bug]")
{
    auto slow = std::make_shared<MockSensorState>();
    slow->readDelayMs = 150;

    auto* mgr = new SensorManager("t_mgr", kPeriodMs, 4096, tskIDLE_PRIORITY + 1);
    mgr->add(std::make_unique<MockSensor>("slow", slow));
    mgr->start();
    sleepMs(60);        // The task is blocked in read() and holds a mutex.
    mgr->stop();

    auto* job = new AddJob{mgr, std::make_shared<MockSensorState>(), xSemaphoreCreateBinary()};
    xTaskCreate(addTask, "add_job", 4096, job, tskIDLE_PRIORITY + 2, nullptr);

    const bool ok = xSemaphoreTake(job->done, pdMS_TO_TICKS(1000)) == pdTRUE;
    // In the event of an error, `mgr` and `job` are intentionally left on the heap: the blocked task
    // still references them, so they must not be deallocated.
    TEST_ASSERT_TRUE_MESSAGE(ok, "add() blocked: mutex left locked by the deleted task");

    vSemaphoreDelete(job->done);
    delete job;
    delete mgr;
}