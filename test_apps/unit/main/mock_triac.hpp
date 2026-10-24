#pragma once
#include "heater_hw.hpp"

struct MockTriac : alco::heater::ITriac {
    int fires = 0;
    int offs = 0;
    void fire() override { ++fires; }
    void off() override { ++offs; }
};