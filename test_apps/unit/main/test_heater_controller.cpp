#include <cmath>
#include "unity.h"
#include "heater_controller.hpp"
#include "mock_triac.hpp"

using namespace alco::heater;

namespace {
SafetyConfig cfg() {
    SafetyConfig c;
    c.minValidC = 0.0f;
    c.maxValidC = 120.0f;
    c.timeoutMs = 5000;
    return c;
}

// P-only controller: power = 2 * (target - temperature), so results are exact.
struct Rig {
    MockTriac triac;
    HeaterController c;
    Rig() : c(triac, 2.0f, 0.0f, 0.0f, cfg()) {}

    int halfCycles(int n) {
        const int before = triac.fires;
        for (int i = 0; i < n; ++i) c.onHalfCycle();
        return triac.fires - before;
    }
};
#define ASSERT_FAULT(expected, r) TEST_ASSERT_EQUAL_INT((int)(expected), (int)(r).c.fault())
}

TEST_CASE("Heater: not started never fires", "[heater]")
{
    Rig r;
    r.c.setTarget(80.0f);
    r.c.onTemperature(20.0f, 100);
    r.c.step(1000, 1.0f);
    TEST_ASSERT_FALSE(r.c.running());
    TEST_ASSERT_EQUAL_INT(0, r.halfCycles(200));
}

TEST_CASE("Heater: no heating before the first valid reading", "[heater]")
{
    Rig r;
    r.c.setTarget(80.0f);
    r.c.start(0);
    r.c.step(1000, 1.0f);
    TEST_ASSERT_TRUE(r.c.running());
    ASSERT_FAULT(Fault::None, r);
    TEST_ASSERT_EQUAL_INT(0, r.halfCycles(100));
}

TEST_CASE("Heater: power is proportional to the error", "[heater]")
{
    Rig r;
    r.c.setTarget(80.0f);
    r.c.start(0);
    r.c.onTemperature(70.0f, 100);
    r.c.step(1000, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, r.c.power());
    TEST_ASSERT_EQUAL_INT(20, r.halfCycles(100));

    r.c.onTemperature(79.0f, 1100);
    r.c.step(2000, 1.0f);
    TEST_ASSERT_EQUAL_INT(2, r.halfCycles(100));
}

TEST_CASE("Heater: at or above target there is no heating", "[heater]")
{
    Rig r;
    r.c.setTarget(80.0f);
    r.c.start(0);
    r.c.onTemperature(80.0f, 100);
    r.c.step(1000, 1.0f);
    TEST_ASSERT_EQUAL_INT(0, r.halfCycles(100));

    r.c.onTemperature(85.0f, 1100);
    r.c.step(2000, 1.0f);
    TEST_ASSERT_EQUAL_INT(0, r.halfCycles(100));
    TEST_ASSERT_TRUE(r.c.running());
}

TEST_CASE("Heater: overtemperature reading cuts power immediately", "[heater][safety]")
{
    Rig r;
    r.c.setTarget(80.0f);
    r.c.start(0);
    r.c.onTemperature(70.0f, 100);
    r.c.step(1000, 1.0f);
    TEST_ASSERT_TRUE(r.halfCycles(100) > 0);

    r.c.onTemperature(130.0f, 1100);          // no step() needed
    TEST_ASSERT_FALSE(r.c.running());
    ASSERT_FAULT(Fault::Overtemp, r);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r.c.power());
    TEST_ASSERT_EQUAL_INT(0, r.halfCycles(100));
    TEST_ASSERT_TRUE(r.triac.offs >= 1);
}

TEST_CASE("Heater: NaN reading cuts power immediately", "[heater][safety]")
{
    Rig r;
    r.c.setTarget(80.0f);
    r.c.start(0);
    r.c.onTemperature(70.0f, 100);
    r.c.step(1000, 1.0f);
    r.c.onTemperature(NAN, 1100);
    TEST_ASSERT_FALSE(r.c.running());
    ASSERT_FAULT(Fault::InvalidReading, r);
    TEST_ASSERT_EQUAL_INT(0, r.halfCycles(100));
}

TEST_CASE("Heater: sensor timeout stops heating", "[heater][safety]")
{
    Rig r;
    r.c.setTarget(80.0f);
    r.c.start(0);
    r.c.onTemperature(70.0f, 100);
    r.c.step(1000, 1.0f);
    TEST_ASSERT_TRUE(r.halfCycles(100) > 0);

    r.c.step(7000, 1.0f);                     // no reading for 6.9 s
    TEST_ASSERT_FALSE(r.c.running());
    ASSERT_FAULT(Fault::SensorTimeout, r);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r.c.power());
    TEST_ASSERT_EQUAL_INT(0, r.halfCycles(100));
}

TEST_CASE("Heater: control watchdog stops firing when step() is not called", "[heater][safety]")
{
    Rig r;
    r.c.setTarget(80.0f);
    r.c.start(0);
    r.c.onTemperature(10.0f, 100);
    r.c.step(1000, 1.0f);                     // error 70 -> 100%
    TEST_ASSERT_EQUAL_INT(300, r.halfCycles(400));   // stops after 300 half-cycles

    r.c.step(1100, 0.1f);                     // control task alive again
    TEST_ASSERT_EQUAL_INT(10, r.halfCycles(10));
}

TEST_CASE("Heater: stop() switches off", "[heater]")
{
    Rig r;
    r.c.setTarget(80.0f);
    r.c.start(0);
    r.c.onTemperature(70.0f, 100);
    r.c.step(1000, 1.0f);
    r.c.stop();
    TEST_ASSERT_FALSE(r.c.running());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r.c.power());
    TEST_ASSERT_EQUAL_INT(0, r.halfCycles(100));
    TEST_ASSERT_TRUE(r.triac.offs >= 1);
}

TEST_CASE("Heater: start() after a fault clears it and heats again", "[heater]")
{
    Rig r;
    r.c.setTarget(80.0f);
    r.c.start(0);
    r.c.onTemperature(130.0f, 100);
    ASSERT_FAULT(Fault::Overtemp, r);

    r.c.start(2000);
    TEST_ASSERT_TRUE(r.c.running());
    ASSERT_FAULT(Fault::None, r);
    r.c.onTemperature(70.0f, 2100);
    r.c.step(3000, 1.0f);
    TEST_ASSERT_EQUAL_INT(20, r.halfCycles(100));
}

TEST_CASE("Heater: setTarget validates its input", "[heater]")
{
    Rig r;
    TEST_ASSERT_TRUE(r.c.setTarget(80.0f));
    TEST_ASSERT_EQUAL_FLOAT(80.0f, r.c.target());
    TEST_ASSERT_FALSE(r.c.setTarget(NAN));
    TEST_ASSERT_FALSE(r.c.setTarget(500.0f));
    TEST_ASSERT_FALSE(r.c.setTarget(-5.0f));
    TEST_ASSERT_EQUAL_FLOAT(80.0f, r.c.target());
}

TEST_CASE("Heater: PID tunings can be read back", "[heater]")
{
    Rig r;
    float kp = 0, ki = 0, kd = 0;
    r.c.getTunings(kp, ki, kd);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, kp);
    r.c.setTunings(1.0f, 2.0f, 3.0f);
    r.c.getTunings(kp, ki, kd);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, kp);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, ki);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, kd);
}

TEST_CASE("Heater: temperature is tracked while stopped, without faults", "[heater]")
{
    Rig r;
    r.c.onTemperature(33.0f, 100);
    TEST_ASSERT_EQUAL_FLOAT(33.0f, r.c.temperature());
    ASSERT_FAULT(Fault::None, r);
}