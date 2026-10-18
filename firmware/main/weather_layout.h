#pragma once
#include "weather.h"
// Today's entry is omitted when three genuinely future days are available.
unsigned weather_carousel_days(const weather_status_t *weather, const char *today,
                               unsigned selected[3]);
