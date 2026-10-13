#pragma once

#include <stdint.h>

static inline int16_t constrain_int16(int16_t amt, int16_t low, int16_t high)
{
    return amt < low ? low : (amt > high ? high : amt);
}
