#pragma once

#include "Falcor.h"
using namespace Falcor::math;

struct WindStructure
{
    int intensity;
    float angle;
    float3 windVector; // horizontal

    WindStructure() : intensity(0), angle(0), windVector(1, 0, 0) {}
    WindStructure(int intensity, int angle) : intensity(intensity), angle(angle * (3.14159 / 180)), windVector(1, 0, 0)
    {}
    void recalcWindVector();
};
