#pragma once
#include "weather.h"
// On success, fills a copy of the caller's snapshot; never publishes partial data.
weather_error_t weather_decode(const char *json, bool daily, weather_status_t *out);
