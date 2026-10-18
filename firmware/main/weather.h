#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef enum {
    WEATHER_WAIT, WEATHER_NO_KEY, WEATHER_NO_CITY, WEATHER_NETWORK,
    WEATHER_TIME, WEATHER_OK, WEATHER_AUTH, WEATHER_PERMISSION,
    WEATHER_CITY, WEATHER_FAILED, WEATHER_RANGE
} weather_error_t;

#define WEATHER_FORECAST_MAX 4
typedef struct {
    char date[11];
    char text[64];
    int low;
    int high;
    int code;
} weather_day_t;

typedef struct {
    uint32_t version;
    char configured_city[97];
    char city[97];
    char text[64];
    char daily_date[11];
    int temperature;
    int code;
    int low;
    int high;
    bool current_valid;
    bool daily_valid;
    time_t updated;
    time_t daily_updated;
    weather_error_t error;
    weather_error_t daily_error;
    unsigned forecast_count;
    weather_day_t forecast[WEATHER_FORECAST_MAX];
} weather_status_t;

void weather_init(void);
void weather_status(weather_status_t *status);
const char *weather_message(weather_error_t error);
