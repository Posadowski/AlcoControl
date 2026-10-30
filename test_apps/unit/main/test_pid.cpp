#include <cmath>
#include "unity.h"
#include <deque>
#include "pid_controller.hpp"

TEST_CASE("PID: clean P", "[pid]")
{
    PIDController pid(2.0f, 0.0f, 0.0f);
    pid.setTarget(50.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, pid.compute(40.0f, 1.0f)); // 2 * 10
}

TEST_CASE("PID: output limited to 0-100", "[pid]")
{
    PIDController pid(100.0f, 0.0f, 0.0f);
    pid.setTarget(100.0f);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, pid.compute(0.0f, 1.0f));   // duży błąd +
    TEST_ASSERT_EQUAL_FLOAT(0.0f,   pid.compute(200.0f, 1.0f)); // błąd -
}

TEST_CASE("PID: The integral increases over time.", "[pid]")
{
    PIDController pid(0.0f, 1.0f, 0.0f);
    pid.setTarget(20.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, pid.compute(10.0f, 1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, pid.compute(10.0f, 1.0f));
}

TEST_CASE("PID: Anti-windup limits the integral.", "[pid]")
{
    PIDController pid(0.0f, 1.0f, 0.0f);
    pid.setTarget(100.0f);
    for (int i = 0; i < 1000; ++i) pid.compute(0.0f, 1.0f); // długo za zimno

    // po przekroczeniu celu całka (max 100) musi dać się "rozładować"
    pid.setTarget(0.0f);
    float out = 100.0f;
    for (int i = 0; i < 5; ++i) out = pid.compute(100.0f, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out);
}

TEST_CASE("PID: A reset clears the state", "[pid]")
{
    PIDController pid(0.0f, 1.0f, 0.0f);
    pid.setTarget(20.0f);
    pid.compute(10.0f, 1.0f);
    pid.reset();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, pid.compute(10.0f, 1.0f));
}

TEST_CASE("PID: dt = 0 cannot deliver full power", "[pid][bug]")
{
    PIDController pid(1.0f, 0.0f, 1.0f);
    pid.setTarget(50.0f);
    pid.compute(50.0f, 1.0f);          // błąd 0
    float out = pid.compute(50.0f, 0.0f); // dt = 0
    TEST_ASSERT_FALSE(std::isnan(out));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, out);
}

TEST_CASE("PID: NaN turns off the heater", "[pid][bug]")
{
    PIDController pid(2.0f, 0.1f, 0.5f);
    pid.setTarget(50.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.compute(NAN, 1.0f));
}

TEST_CASE("PID: the integral can hold a steady power, not only 10 percent", "[pid][integral]")
{
    // Old behaviour: ki * clamp(integral, +-100) limited the I term to ki*100 = 10 %.
    PIDController pid(2.0f, 0.1f, 0.0f);
    pid.setTarget(80.0f);
    float out = 0;
    for (int i = 0; i < 400; ++i) out = pid.compute(75.0f, 1.0f);   // constant 5 C error
    TEST_ASSERT_TRUE(pid.integralTerm() > 40.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, out);
}

TEST_CASE("PID: the integral does not wind up while the output is saturated", "[pid][integral]")
{
    PIDController pid(1.0f, 1.0f, 0.0f);
    pid.setTarget(100.0f);
    for (int i = 0; i < 100; ++i) pid.compute(0.0f, 1.0f);          // P alone = 100 %
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.integralTerm());
}

TEST_CASE("PID: integral band blocks integration far from the target", "[pid][integral]")
{
    PIDController pid(0.0f, 1.0f, 0.0f);
    pid.setIntegralBand(5.0f);
    pid.setTarget(80.0f);
    for (int i = 0; i < 20; ++i) pid.compute(60.0f, 1.0f);          // error 20: outside
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.integralTerm());
    pid.compute(76.0f, 1.0f);                                       // error 4: inside
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.0f, pid.integralTerm());
}

TEST_CASE("PID: changing ki at run time does not make the output jump", "[pid][integral]")
{
    PIDController pid(0.0f, 1.0f, 0.0f);
    pid.setTarget(20.0f);
    pid.compute(10.0f, 1.0f);                                       // I term = 10
    TEST_ASSERT_TRUE(pid.setTunings(0.0f, 5.0f, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, pid.compute(20.0f, 1.0f));   // error 0
}

TEST_CASE("PID: derivative acts on the measurement, a setpoint change gives no kick", "[pid][deriv]")
{
    PIDController pid(0.0f, 0.0f, 10.0f);
    pid.setTarget(50.0f);
    pid.compute(50.0f, 1.0f);
    pid.setTarget(80.0f);                                           // step in the setpoint
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.compute(50.0f, 1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, pid.compute(48.0f, 1.0f));   // falling: +20
}

TEST_CASE("PID: invalid tunings are rejected and the old ones stay", "[pid][error]")
{
    PIDController pid(2.0f, 0.0f, 0.0f);
    pid.setTarget(50.0f);
    TEST_ASSERT_FALSE(pid.setTunings(NAN, 0.0f, 0.0f));
    TEST_ASSERT_FALSE(pid.setTunings(-1.0f, 0.0f, 0.0f));
    TEST_ASSERT_FALSE(pid.setTunings(1.0f, INFINITY, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, pid.compute(40.0f, 1.0f));
    TEST_ASSERT_TRUE(pid.setTunings(1.0f, 0.0f, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, pid.compute(40.0f, 1.0f));
}

TEST_CASE("PID: an invalid dt does not change the integral", "[pid][error]")
{
    PIDController pid(0.0f, 1.0f, 0.0f);
    pid.setTarget(20.0f);
    pid.compute(10.0f, 1.0f);
    const float before = pid.integralTerm();
    pid.compute(10.0f, 0.0f);
    pid.compute(10.0f, -1.0f);
    pid.compute(10.0f, NAN);
    TEST_ASSERT_EQUAL_FLOAT(before, pid.integralTerm());
}

TEST_CASE("PID: custom output limits", "[pid]")
{
    PIDController pid(100.0f, 0.0f, 0.0f);
    pid.setOutputLimits(10.0f, 50.0f);
    pid.setTarget(50.0f);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, pid.compute(0.0f, 1.0f));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, pid.compute(100.0f, 1.0f));
}

TEST_CASE("PID: the integral keeps unwinding while the output sits at the lower limit", "[pid][integral]")
{
    PIDController pid(2.0f, 1.0f, 0.0f);
    pid.setTarget(80.0f);
    pid.compute(65.0f, 1.0f);                                       // error 15 -> I term = 15
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.0f, pid.integralTerm());

    pid.setTarget(60.0f);                                           // now 20 C above the target
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.compute(80.0f, 1.0f));        // P = -40, output clamped to 0
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.integralTerm());              // 15 - 20, clamped to 0
}

// ---------------- closed loop on a simulated boiler ----------------
namespace {
// First-order boiler with heat loss and optional dead time (heater -> sensor delay).
struct Boiler {
    float T = 20.0f;
    float ambient = 20.0f;
    float gain = 0.01f;            // C/s per percent of power (100 % -> 1 C/s)
    float loss = 0.005f;           // 1/s
    std::deque<float> pipe;
    explicit Boiler(int deadTimeS = 0) : pipe(deadTimeS, 0.0f) {}
    void step(float powerPct, float dt) {
        pipe.push_back(powerPct);
        const float p = pipe.front();
        pipe.pop_front();
        T += (gain * p - loss * (T - ambient)) * dt;
    }
};

struct SimResult { float finalT; float maxT; float power; };

SimResult simulate(PIDController& pid, float target, int seconds, int deadTimeS) {
    Boiler b(deadTimeS);
    pid.setTarget(target);
    SimResult r{b.T, b.T, 0.0f};
    for (int i = 0; i < seconds; ++i) {
        const float p = pid.compute(b.T, 1.0f);
        b.step(p, 1.0f);
        if (b.T > r.maxT) r.maxT = b.T;
        r.power = p;
    }
    r.finalT = b.T;
    return r;
}
}

TEST_CASE("PID loop: reaches the target with no steady-state error", "[pid][sim]")
{
    PIDController pid(2.0f, 0.02f, 0.5f);
    const SimResult r = simulate(pid, 80.0f, 4000, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 80.0f, r.finalT);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 30.0f, r.power);        // holding 60 C * 0.005 / 0.01
}

TEST_CASE("PID loop: previous tunings (2, 0.1, 0.5) now also reach the target", "[pid][sim]")
{
    // With the old clamp the I term was capped at 10 %, so the boiler stalled near 70 C.
    PIDController pid(2.0f, 0.1f, 0.5f);
    const SimResult r = simulate(pid, 80.0f, 4000, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 80.0f, r.finalT);
}

TEST_CASE("PID loop: conservative tunings stay stable with a 30 s dead time", "[pid][sim]")
{
    PIDController pid(2.0f, 0.02f, 0.5f);
    const SimResult r = simulate(pid, 80.0f, 6000, 30);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 80.0f, r.finalT);
    TEST_ASSERT_TRUE_MESSAGE(r.maxT < 90.0f, "overshoot larger than 10 C");
}