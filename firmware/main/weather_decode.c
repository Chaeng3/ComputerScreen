#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "weather_decode.h"

static const char *field(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static bool number(const cJSON *object, const char *name, int minimum, int maximum, int *out)
{
    const char *value = field(object, name);
    if (!value || !*value) return false;
    char *end;
    long parsed = strtol(value, &end, 10);
    if (*end || parsed < minimum || parsed > maximum) return false;
    *out = (int)parsed;
    return true;
}

static weather_error_t api_error(const char *code)
{
    if (!code) return WEATHER_FAILED;
    if (!strcmp(code, "AP010001")) return WEATHER_RANGE;
    if (!strcmp(code, "AP010003") || !strcmp(code, "AP010004")) return WEATHER_AUTH;
    if (!strcmp(code, "AP010002") || !strcmp(code, "AP010012") ||
        !strcmp(code, "AP010013")) return WEATHER_PERMISSION;
    if (!strcmp(code, "AP010006") || !strcmp(code, "AP010010")) return WEATHER_CITY;
    return WEATHER_FAILED;
}

weather_error_t weather_decode(const char *json, bool daily, weather_status_t *out)
{
    cJSON *root = cJSON_ParseWithOpts(json, NULL, true);
    if (!root) return WEATHER_FAILED;
    weather_error_t result = WEATHER_FAILED;
    const char *status = field(root, "status_code");
    if (status) { result = api_error(status); goto done; }
    cJSON *first = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "results"), 0);
    cJSON *location = cJSON_GetObjectItemCaseSensitive(first, "location");
    const char *country = field(location, "country");
    const char *city = field(location, "name");
    if (!country || strcmp(country, "CN")) { result = WEATHER_CITY; goto done; }
    if (!city || !city[0] || strlen(city) >= sizeof(out->city)) goto done;
    if (daily) {
        cJSON *days = cJSON_GetObjectItemCaseSensitive(first, "daily");
        int count = cJSON_GetArraySize(days);
        if (!cJSON_IsArray(days) || count < 1) goto done;
        if (count > WEATHER_FORECAST_MAX) count = WEATHER_FORECAST_MAX;
        weather_day_t parsed[WEATHER_FORECAST_MAX] = {0};
        time_t now = time(NULL);
        struct tm local;
        localtime_r(&now, &local);
        for (int i = 0; i < count; ++i) {
            cJSON *day = cJSON_GetArrayItem(days, i);
            const char *date = field(day, "date");
            const char *text = field(day, "text_day");
            char expected[11];
            struct tm expected_day = local;
            expected_day.tm_mday += i;
            expected_day.tm_hour = 12;
            expected_day.tm_isdst = -1;
            mktime(&expected_day);
            strftime(expected, sizeof(expected), "%Y-%m-%d", &expected_day);
            if (!date || strcmp(date, expected) || !text || !*text || strlen(text) >= sizeof(parsed[i].text) ||
                !number(day, "low", -100, 70, &parsed[i].low) ||
                !number(day, "high", -100, 70, &parsed[i].high) || parsed[i].low > parsed[i].high ||
                !number(day, "code_day", 0, 99, &parsed[i].code)) goto done;
            strlcpy(parsed[i].date, date, sizeof(parsed[i].date));
            strlcpy(parsed[i].text, text, sizeof(parsed[i].text));
        }
        // Commit the complete validated forecast together.
        memcpy(out->forecast, parsed, sizeof(parsed));
        out->forecast_count = count;
        out->low = parsed[0].low;
        out->high = parsed[0].high;
        strlcpy(out->daily_date, parsed[0].date, sizeof(out->daily_date));
        out->daily_updated = now;
        out->daily_valid = true;
    } else {
        cJSON *current = cJSON_GetObjectItemCaseSensitive(first, "now");
        const char *text = field(current, "text");
        const char *updated = field(first, "last_update");
        int temp, code;
        if (!text || !text[0] || strlen(text) >= sizeof(out->text) || !updated ||
            !number(current, "temperature", -100, 70, &temp) ||
            !number(current, "code", 0, 99, &code)) goto done;
        int y, m, d, h, min, sec;
        if (sscanf(updated, "%d-%d-%dT%d:%d:%d+08:00", &y, &m, &d, &h, &min, &sec) != 6 ||
            y < 2024 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31 ||
            h < 0 || h > 23 || min < 0 || min > 59 || sec < 0 || sec > 59 ||
            !strstr(updated, "+08:00")) goto done;
        struct tm provider_time = {.tm_year=y-1900, .tm_mon=m-1, .tm_mday=d,
                                   .tm_hour=h, .tm_min=min, .tm_sec=sec, .tm_isdst=-1};
        time_t timestamp = mktime(&provider_time);
        if (timestamp <= 0 || timestamp > time(NULL) + 300) goto done;
        out->updated = timestamp;
        out->temperature = temp;
        out->code = code;
        strlcpy(out->city, city, sizeof(out->city));
        strlcpy(out->text, text, sizeof(out->text));
        out->current_valid = true;
    }
    result = WEATHER_OK;
done:
    cJSON_Delete(root);
    return result;
}
