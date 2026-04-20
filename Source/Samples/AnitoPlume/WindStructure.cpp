#include "WindStructure.h"

void WindStructure::recalcWindVector()
{
    windVector = float3(cos(angle), sin(angle), 0) * (float)intensity;
}
