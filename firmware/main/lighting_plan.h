#pragma once
#include <stdbool.h>

// Night spans midnight. Until the clock is synchronized, use the dimmer value
// rather than briefly lighting a dark room at daytime brightness.
static inline unsigned lighting_target(unsigned day, unsigned night, bool automatic,
                                       unsigned day_start, unsigned night_start,
                                       unsigned minute, bool time_valid)
{
    if (!automatic) return day;
    if (!time_valid) return day < night ? day : night;
    return minute < day_start || minute >= night_start ? night : day;
}
