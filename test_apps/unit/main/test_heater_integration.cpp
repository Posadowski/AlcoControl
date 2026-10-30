#include <cmath>
#include <cstdint>
#include "unity.h"
#include "heater_controller.hpp"

using namespace alco::heater;

// ======================= simulation harness =======================
// Virtual time: one tick = one AC half-cycle (10 ms at 50 Hz). No real waiting.
//   - zero-cross ISR      -> HeaterController::onHalfCycle()  every tick
//   - sensor task         -> HeaterController::onTemperature() every sensorPeriodMs
//   - control task        -> HeaterController::step()          every 1000 ms
//   - triac               -> counted; every fired half-cycle heats the boiler
namespace {

struct CountingTriac : ITriac {
    bool firedNow = false;
    int  offs = 0;
    void fire() override { firedNow = true; }
    void off() override  { ++offs; }
};

struct Plant {                          // first-order boiler with heat loss
    float T = 20.0f;
    float ambient = 20.0f;
    float gainPerFire = 0.01f;          // C per fired half-cycle (100 % power = 1 C/s)
    float loss = 0.005f;                // 1/s
    float externalHeat = 0.0f;          // C/s, heat that does not come from the controller
    void halfCycle(bool fired) {
        T += gainPerFire * (fired ? 1.0f : 0.0f)
           + externalHeat * 0.01f
           - loss * (T - ambient) * 0.01f;
    }
};

enum class SensorMode { Ok, Dead, NotANumber, Stuck };

SafetyConfig safetyCfg() {
    SafetyConfig c;
    c.minValidC = -20.0f;
    c.maxValidC = 105.0f;
    c.timeoutMs = 5000;
    return c;
}

float quantize(float t) { return std::round(t / 0.0625f) * 0.0625f; }   // DS18B20, 12 bit

struct Sim {
    CountingTriac triac;
    HeaterController ctl;
    Plant plant;
    uint64_t nowMs = 0;

    SensorMode sensor = SensorMode::Ok;
    float stuckValue = 40.0f;
    uint32_t sensorPeriodMs = 1000;
    uint32_t sensorPhaseMs = 500;       // the sensor and control tasks are not synchronised
    bool controlAlive = true;
    bool mainsPresent = true;

    long   totalFires = 0;
    uint64_t lastFireMs = 0;
    uint64_t faultAtMs = 0;
    bool   faultSeen = false;
    float  maxT = 20.0f;

    // Keep the PID values in sync with HeaterManager (2.0 / 0.02 / 0.5).
    Sim() : ctl(triac, 2.0f, 0.02f, 0.5f, safetyCfg()) {}

    void tick() {
        triac.firedNow = false;
        if (mainsPresent) ctl.onHalfCycle();
        if (triac.firedNow) { ++totalFires; lastFireMs = nowMs; }
        plant.halfCycle(triac.firedNow);
        if (plant.T > maxT) maxT = plant.T;
        nowMs += 10;

        if (nowMs % sensorPeriodMs == sensorPhaseMs) {
            switch (sensor) {
                case SensorMode::Ok:         ctl.onTemperature(quantize(plant.T), nowMs); break;
                case SensorMode::NotANumber: ctl.onTemperature(NAN, nowMs);               break;
                case SensorMode::Stuck:      ctl.onTemperature(stuckValue, nowMs);        break;
                case SensorMode::Dead:       break;                // SensorManager only logs failures
            }
        }
        if (nowMs % 1000 == 0 && controlAlive) ctl.step(nowMs, 1.0f);

        if (!faultSeen && ctl.fault() != Fault::None) { faultSeen = true; faultAtMs = nowMs; }
    }

    void run(int seconds) { for (int i = 0; i < seconds * 100; ++i) tick(); }

    // Run until the first fault is seen (or the limit is reached); returns true if a fault occurred.
    bool runUntilFault(int maxSeconds) {
        for (int i = 0; i < maxSeconds * 100 && !faultSeen; ++i) tick();
        return faultSeen;
    }

    long firesDuring(int seconds) {
        const long before = totalFires;
        run(seconds);
        return totalFires - before;
    }
};

void startHeating(Sim& s, float target = 80.0f) {
    s.ctl.setTarget(target);
    s.ctl.start(s.nowMs);
}
#define ASSERT_FAULT(expected, s) TEST_ASSERT_EQUAL_INT((int)(expected), (int)(s).ctl.fault())

} // namespace

// ======================= normal operation =======================

TEST_CASE("Integration: heats from cold to the target and holds it", "[integration]")
{
    Sim s;
    startHeating(s, 80.0f);
    s.run(900);

    TEST_ASSERT_TRUE(s.ctl.running());
    ASSERT_FAULT(Fault::None, s);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 80.0f, s.plant.T);
    TEST_ASSERT_TRUE_MESSAGE(s.maxT < 85.0f, "overshoot larger than 5 C");
}

TEST_CASE("Integration: the fired fraction matches the commanded power", "[integration]")
{
    Sim s;
    startHeating(s, 80.0f);
    s.run(900);                                       // steady state, ~30 % holding power

    const long fires = s.firesDuring(100);            // 100 s = 10000 half-cycles
    const float fraction = 100.0f * static_cast<float>(fires) / 10000.0f;
    TEST_ASSERT_FLOAT_WITHIN(3.0f, 30.0f, fraction);
    TEST_ASSERT_FLOAT_WITHIN(3.0f, s.ctl.power(), fraction);
}

TEST_CASE("Integration: power stays smooth at steady state (no chattering)", "[integration]")
{
    Sim s;
    startHeating(s, 80.0f);
    s.run(900);

    float lo = 100.0f, hi = 0.0f;
    for (int i = 0; i < 200; ++i) {
        s.run(1);
        lo = std::fmin(lo, s.ctl.power());
        hi = std::fmax(hi, s.ctl.power());
    }
    TEST_ASSERT_TRUE_MESSAGE(hi - lo < 5.0f, "power moves by more than 5 points at steady state");
}

TEST_CASE("Integration: lowering the setpoint stops heating until it cools", "[integration]")
{
    Sim s;
    startHeating(s, 80.0f);
    s.run(900);

    TEST_ASSERT_TRUE(s.ctl.setTarget(60.0f));
    s.maxT = s.plant.T;                                       // track the maximum from now on
    s.run(2);                                                 // the control task applies the new power
    TEST_ASSERT_EQUAL_INT(0, (int)s.firesDuring(25));         // far above the new target: no heating
    s.run(600);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 60.0f, s.plant.T);
    TEST_ASSERT_TRUE_MESSAGE(s.maxT < 81.0f, "heated while above the new target");
    ASSERT_FAULT(Fault::None, s);
}

TEST_CASE("Integration: occasional missing readings do not trip the heater", "[integration]")
{
    // SensorManager does not report failed reads at all, so a flaky bus shows up as gaps.
    Sim s;
    s.sensorPeriodMs = 3000;                                  // one reading per 3 s (< 5 s timeout)
    startHeating(s, 80.0f);
    s.run(900);

    ASSERT_FAULT(Fault::None, s);
    TEST_ASSERT_TRUE(s.ctl.running());
    TEST_ASSERT_FLOAT_WITHIN(1.5f, 80.0f, s.plant.T);
}

// ======================= sensor failures =======================

TEST_CASE("Integration: dead sensor -> heater off within the timeout", "[integration][safety]")
{
    Sim s;
    startHeating(s, 80.0f);
    s.run(200);
    TEST_ASSERT_TRUE(s.firesDuring(10) > 0);                  // it was heating

    const uint64_t failedAt = s.nowMs;
    s.sensor = SensorMode::Dead;
    s.run(30);

    ASSERT_FAULT(Fault::SensorTimeout, s);
    TEST_ASSERT_FALSE(s.ctl.running());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.ctl.power());
    // last reading + 5 s timeout + one control period, plus a small margin
    TEST_ASSERT_TRUE_MESSAGE(s.lastFireMs <= failedAt + 7000, "heater kept firing for more than 7 s");
    TEST_ASSERT_EQUAL_INT(0, (int)s.firesDuring(60));         // and stays off
}

TEST_CASE("Integration: a NaN reading cuts power immediately", "[integration][safety]")
{
    Sim s;
    startHeating(s, 80.0f);
    s.run(200);
    s.sensor = SensorMode::NotANumber;
    s.run(5);

    ASSERT_FAULT(Fault::InvalidReading, s);
    TEST_ASSERT_FALSE(s.ctl.running());
    TEST_ASSERT_TRUE_MESSAGE(s.lastFireMs <= s.faultAtMs, "fired after the fault was detected");
    TEST_ASSERT_TRUE(s.triac.offs >= 1);
    TEST_ASSERT_EQUAL_INT(0, (int)s.firesDuring(60));
}

// ======================= overtemperature =======================

TEST_CASE("Integration: external heat above the limit trips the heater", "[integration][safety]")
{
    Sim s;
    startHeating(s, 80.0f);
    s.run(900);                                               // holding at 80 C

    s.plant.externalHeat = 1.0f;                              // 1 C/s from something else
    TEST_ASSERT_TRUE(s.runUntilFault(120));

    ASSERT_FAULT(Fault::Overtemp, s);
    TEST_ASSERT_FALSE(s.ctl.running());
    TEST_ASSERT_TRUE_MESSAGE(s.lastFireMs <= s.faultAtMs, "fired after the overtemperature fault");
    TEST_ASSERT_TRUE(s.faultAtMs > 0);
}

TEST_CASE("Integration: a heater that is stuck on keeps heating after the trip",
          "[integration][safety][characterization]")
{
    // Documents the limit of software protection: the controller commands "off", but a
    // shorted triac or a welded relay still conducts. Only an independent hardware
    // cut-off (thermal fuse / safety thermostat in series with the heater) stops this.
    Sim s;
    startHeating(s, 80.0f);
    s.run(900);

    s.plant.externalHeat = 1.0f;                              // models the stuck-on element
    TEST_ASSERT_TRUE(s.runUntilFault(120));
    ASSERT_FAULT(Fault::Overtemp, s);
    const float atTrip = s.plant.T;                           // about 105 C
    s.run(60);
    TEST_ASSERT_TRUE_MESSAGE(s.plant.T > atTrip + 20.0f, "expected the boiler to keep heating");
}

// ======================= other failures =======================

TEST_CASE("Integration: a hung control task stops the heater via the watchdog", "[integration][safety]")
{
    Sim s;
    startHeating(s, 80.0f);
    s.run(5);                                                 // cold boiler: ~100 % power
    TEST_ASSERT_TRUE(s.firesDuring(3) > 250);

    s.controlAlive = false;                                   // step() is no longer called
    const uint64_t hungAt = s.nowMs;
    s.run(30);
    TEST_ASSERT_TRUE_MESSAGE(s.lastFireMs <= hungAt + 3100, "kept firing after the watchdog limit");
    TEST_ASSERT_EQUAL_INT(0, (int)s.firesDuring(30));
}

TEST_CASE("Integration: no mains zero-cross means no firing, and it resumes by itself",
          "[integration]")
{
    Sim s;
    startHeating(s, 80.0f);
    s.run(100);

    s.mainsPresent = false;
    TEST_ASSERT_EQUAL_INT(0, (int)s.firesDuring(20));
    TEST_ASSERT_TRUE(s.ctl.running());
    ASSERT_FAULT(Fault::None, s);

    s.mainsPresent = true;
    TEST_ASSERT_TRUE(s.firesDuring(10) > 0);
}

TEST_CASE("Integration: stop() is immediate and the boiler cools", "[integration]")
{
    Sim s;
    startHeating(s, 80.0f);
    s.run(300);
    const float hot = s.plant.T;

    s.ctl.stop();
    TEST_ASSERT_EQUAL_INT(0, (int)s.firesDuring(60));
    TEST_ASSERT_TRUE(s.plant.T < hot);
    TEST_ASSERT_FALSE(s.ctl.running());
}

TEST_CASE("Integration: start() after a fault clears it and heating resumes", "[integration][safety]")
{
    Sim s;
    startHeating(s, 80.0f);
    s.run(200);
    s.sensor = SensorMode::NotANumber;
    s.run(5);
    ASSERT_FAULT(Fault::InvalidReading, s);

    s.sensor = SensorMode::Ok;                                // the sensor is fixed
    s.ctl.start(s.nowMs);                                     // the operator restarts
    TEST_ASSERT_TRUE(s.ctl.running());
    ASSERT_FAULT(Fault::None, s);
    TEST_ASSERT_TRUE(s.firesDuring(20) > 0);
}

// ======================= limits of the software protection =======================

TEST_CASE("Integration: a sensor stuck at a low value is NOT detected by software",
          "[integration][safety][characterization]")
{
    // The sensor reports a constant 40 C while the boiler really heats up. The software sees
    // valid data and keeps heating at full power until the boiler reaches the limit, which it
    // can no longer see. This test pins that behaviour; it is the reason for an independent
    // hardware cut-off (or a plausibility check, see the notes).
    Sim s;
    s.sensor = SensorMode::Stuck;
    s.stuckValue = 40.0f;
    startHeating(s, 80.0f);
    s.run(600);

    ASSERT_FAULT(Fault::None, s);
    TEST_ASSERT_TRUE(s.ctl.running());
    TEST_ASSERT_TRUE_MESSAGE(s.plant.T > 106.0f, "expected the real temperature to run away");
}