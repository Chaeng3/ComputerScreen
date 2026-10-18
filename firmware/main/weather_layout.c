#include <string.h>
#include "weather_layout.h"

unsigned weather_carousel_days(const weather_status_t *weather, const char *today,
                               unsigned selected[3])
{
    unsigned future[3], future_count = 0;
    unsigned count = 0;
    for (unsigned i = 0; i < weather->forecast_count && i < WEATHER_FORECAST_MAX; ++i) {
        if (strcmp(weather->forecast[i].date, today) < 0) continue;
        if (count < 3) selected[count++] = i;
        if (strcmp(weather->forecast[i].date, today) > 0 && future_count < 3) future[future_count++] = i;
    }
    if (future_count == 3) memcpy(selected, future, sizeof(future));
    return future_count == 3 ? 3 : count;
}
