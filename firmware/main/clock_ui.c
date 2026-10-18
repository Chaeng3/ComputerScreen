#include <stdio.h>
#include <string.h>
#include <time.h>
#include "display.h"
#include "settings.h"
#include "network.h"
#include "weather.h"
#include "clock_ui.h"
#include "weather_layout.h"
#include "esp_timer.h"

static void center(int y, const char *text, uint16_t color)
{
    int x = (320 - display_utf8_width(text)) / 2;
    display_utf8(x < 12 ? 12 : x, y, text, color);
}

static void setup_screen(const network_status_t *net)
{
    display_text_shadow(false);
    display_clear(0x0000);
    center(10, "连接手机，设置副屏", 0xffff);
    center(44, "设置热点", 0x6eff);
    center(68, net->ap_name, 0xffff);
    char text[80];
    snprintf(text, sizeof(text), "密码 %s", net->ap_password);
    center(98, text, 0xffff);
    center(136, "浏览器打开", 0x6eff);
    center(160, "192.168.4.1", 0xffff);
    center(207, "手机完成设置后自动开始", 0x9cf3);
    display_present();
}

static void shifted_center(int shift, int y, const char *text, uint16_t color)
{
    display_utf8(shift + (320 - display_utf8_width(text)) / 2, y, text, color);
}

// The lower corners are matching 84x84 layout areas; no outlines are drawn.
#define WEATHER_SIDE 84
#define WEATHER_TOP 154
#define WEATHER_LEFT 8
#define WEATHER_RIGHT 228

static void weather_sun(int cx, int cy, int radius, bool crescent)
{
    display_circle(cx, cy, radius, 0xfdc5);
    if (crescent) {
        display_circle(cx + radius / 2, cy - radius / 2, radius - 2, 0x0000);
    } else {
        display_rect(cx - 2, cy - radius - 10, 4, 6, 0xfdc5);
        display_rect(cx - 2, cy + radius + 4, 4, 6, 0xfdc5);
        display_rect(cx - radius - 10, cy - 2, 6, 4, 0xfdc5);
        display_rect(cx + radius + 4, cy - 2, 6, 4, 0xfdc5);
        int diagonal = radius + 1;
        for (int dx = -1; dx <= 1; dx += 2)
            for (int dy = -1; dy <= 1; dy += 2)
                display_circle(cx + dx * diagonal, cy + dy * diagonal, 2, 0xfdc5);
    }
}

static void weather_icon(int x, int y, int code)
{
    bool sun = code >= 0 && code <= 3;
    bool cloudy = code >= 4 && code <= 9;
    bool snow = code >= 20 && code <= 25;
    bool rain = code >= 10 && code <= 19;
    bool crescent = code == 1 || code == 3 || code == 5 || code == 7;
    if (sun) {
        weather_sun(x + 42, y + 42, 23, crescent);
        return;
    }
    if (code == 4 || code == 5 || code == 7) weather_sun(x + 30, y + 29, 16, crescent);
    if (cloudy || rain || snow || code >= 26) {
        int cy = y + (rain || snow ? 36 : 44);
        display_circle(x + 26, cy, 15, 0xe73e);
        display_circle(x + 44, cy - 8, 20, 0xe73e);
        display_circle(x + 61, cy + 1, 13, 0xe73e);
        display_rect(x + 25, cy, 38, 17, 0xe73e);
    }
    if (rain) {
        for (int column = 0; column < 3; ++column) {
            if ((code == 11 || code == 12) && column == 1) continue;
            int cx = x + 26 + column * 17;
            for (int row = 0; row < 12; ++row) display_rect(cx - row / 3, y + 60 + row, 3, 1, 0x5e7f);
        }
        if (code == 11 || code == 12) {
            for (int row = 0; row < 22; ++row)
                display_rect(x + 44 - row / 2 + (row >= 10 ? 5 : 0), y + 53 + row, row < 10 ? 7 : 4, 1, 0xfdc5);
        }
    } else if (snow) {
        for (int cx = x + 24; cx <= x + 60; cx += 18) {
            display_rect(cx - 4, y + 67, 9, 2, 0xffff);
            display_rect(cx, y + 63, 2, 10, 0xffff);
            display_circle(cx, y + 68, 3, 0xffff);
        }
    }
}

static void forecast_label(const char *date, const struct tm *local, char *label, size_t capacity)
{
    for (int offset=0; offset<3; ++offset) {
        struct tm day = *local;
        day.tm_mday += offset;
        day.tm_hour = 12;
        day.tm_isdst = -1;
        mktime(&day);
        char formatted[11];
        strftime(formatted, sizeof(formatted), "%Y-%m-%d", &day);
        if (!strcmp(date, formatted)) {
            snprintf(label, capacity, "%s", offset == 0 ? "今天" : offset == 1 ? "明天" : "后天");
            return;
        }
    }
    int year, month, day;
    if (sscanf(date, "%d-%d-%d", &year, &month, &day) == 3)
        snprintf(label, capacity, "%d月%d日", month, day);
    else snprintf(label, capacity, "预报");
}

static void weather_page(int page, int shift, const unsigned selected[3], const weather_status_t *weather,
                         const struct tm *local, bool has_time, uint16_t accent)
{
    if (page == 0 && !weather->current_valid) {
        shifted_center(shift, 179, weather_message(weather->error), 0xffff);
        return;
    }
    const weather_day_t *forecast = page ? &weather->forecast[selected[page - 1]] : NULL;
    const char *description = forecast ? forecast->text : weather->text;
    int code = forecast ? forecast->code : weather->code;
    char value[40], line[160];
    weather_icon(shift + WEATHER_LEFT, WEATHER_TOP, code);
    int description_scale = display_utf8_width(description) <= 68 ? 2 : 1;
    display_utf8_box(shift + WEATHER_RIGHT, WEATHER_TOP, WEATHER_SIDE, WEATHER_SIDE,
                     description, description_scale, 0xffff);
    if (forecast) {
        snprintf(value, sizeof(value), "%d° / %d°", forecast->low, forecast->high);
        display_utf8_box(shift + 96, 161, 128, 28, value, 1, 0xffff);
    } else {
        snprintf(value, sizeof(value), "%d", weather->temperature);
        int number_width = ((int)strlen(value) * 6 - 1) * 4;
        int total_width = number_width + 4 + display_utf8_width("℃");
        int x = shift + 160 - total_width / 2;
        display_text(x, 159, value, 4, 0xffff);
        display_utf8(x + number_width + 4, 163, "℃", 0xffff);
    }

    time_t now = time(NULL);
    bool stale = !has_time || (forecast ? now < weather->daily_updated || now - weather->daily_updated > 21600 :
        now < weather->updated || now - weather->updated > 3600);
    if (forecast) {
        char label[32];
        forecast_label(forecast->date, local, label, sizeof(label));
        display_utf8_box(shift + 96, 188, 128, 20, label, 1, 0xffff);
    } else {
        char today[11];
        strftime(today, sizeof(today), "%Y-%m-%d", local);
        if (has_time && weather->daily_valid && !strcmp(today, weather->daily_date)) {
            snprintf(line, sizeof(line), "%d° / %d°", weather->low, weather->high);
            display_utf8_box(shift + 96, 188, 128, 20, line, 1, 0xffff);
        }
    }
    snprintf(line, sizeof(line), "%s%s", weather->city, stale ? " 待更新" : "");
    display_utf8_box(shift + 96, 210, 128, 20, line, 1, stale ? accent : 0xffff);
}

void clock_ui_render(void)
{
    static time_t last_time = (time_t)-1;
    static weather_status_t last_weather;
    static bool last_transition;
    network_status_t net;
    network_status(&net);
    const app_settings_t *settings = settings_get();
    time_t now = time(NULL);
    if (!settings->ssid[0]) {
        if (now != last_time) setup_screen(&net);
        last_time = now;
        return;
    }
    struct tm local;
    localtime_r(&now, &local);
    bool has_time = net.time_synced && local.tm_year >= 124;
    weather_status_t weather;
    weather_status(&weather);
    char today[11];
    strftime(today, sizeof(today), "%Y-%m-%d", &local);
    unsigned selected[3];
    unsigned days = has_time ? weather_carousel_days(&weather, today, selected) : 0;
    unsigned pages = days + 1;
    int64_t millis = esp_timer_get_time() / 1000;
    unsigned sequence = millis / 10000;
    unsigned elapsed = millis % 10000;
    unsigned page = sequence % pages;
    bool transition = pages > 1 && sequence > 0 && elapsed < 650;
    bool clock_changed = now != last_time;
    if (!clock_changed && !transition && !last_transition && !memcmp(&weather, &last_weather, sizeof(weather))) return;
    uint16_t accent = 0x5e7f;
    if (has_time) {
        unsigned minute = local.tm_hour * 60 + local.tm_min;
        if (minute >= settings->day_start && minute < settings->dusk_start) accent = 0x6eff;
        else if (minute >= settings->dusk_start && minute < settings->night_start) accent = 0xfd6e;
    }
    display_clear(0x0000);
    display_text_shadow(false);
    char text[100];
    static const char *weekdays[] = {"日","一","二","三","四","五","六"};
    if (has_time) {
        snprintf(text, sizeof(text), "%d月%d日", local.tm_mon + 1, local.tm_mday);
        display_utf8(20, 12, text, 0xffff);
        snprintf(text, sizeof(text), "星期%s", weekdays[local.tm_wday]);
        display_utf8(300 - display_utf8_width(text), 12, text, 0xffff);
    } else center(12, "正在校准时间", 0xffff);
    // A compact colon preserves the large digits while leaving room for seconds.
    // The complete time group occupies x=20..299, with shared bottom alignment.
    if (has_time) strftime(text, sizeof(text), "%H", &local);
    else snprintf(text, sizeof(text), "--");
    display_text(20, 58, text, 10, 0xffff);
    display_rect(140, 78, 10, 10, 0xffff);
    display_rect(140, 108, 10, 10, 0xffff);
    if (has_time) strftime(text, sizeof(text), "%M", &local);
    else snprintf(text, sizeof(text), "--");
    display_text(160, 58, text, 10, 0xffff);
    if (has_time) strftime(text, sizeof(text), "%S", &local);
    else snprintf(text, sizeof(text), "--");
    display_text(278, 114, text, 2, accent);
    display_rect(20, 151, 280, 1, accent);
    if (transition) {
        int offset = elapsed * 320 / 650;
        unsigned previous = (sequence - 1) % pages;
        weather_page(previous, -offset, selected, &weather, &local, has_time, accent);
        weather_page(page, 320 - offset, selected, &weather, &local, has_time, accent);
    } else weather_page(page, 0, selected, &weather, &local, has_time, accent);
    if (pages > 1) {
        for (unsigned i=0; i<pages; ++i) display_circle(160 - (int)(pages - 1) * 6 + i * 12, 236, 2,
                                                      i == page ? accent : 0x4a6b);
    }
    if (clock_changed) display_present();
    else display_present_region(153, 87);
    last_time = now;
    last_weather = weather;
    last_transition = transition;
}
