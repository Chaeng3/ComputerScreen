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
#include "case_environment.h"

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

#define TEMPERATURE_TOP 104
#define TEMPERATURE_BOTTOM 178
#define WEATHER_TOP 182
#define WEATHER_TEMPERATURE_TOP (WEATHER_TOP + 11)
#define WEATHER_TEMPERATURE_BOTTOM (WEATHER_TEMPERATURE_TOP + 21)

static void weather_icon(int x, int y, int code)
{
    bool sun = code >= 0 && code <= 3;
    bool cloudy = code >= 4 && code <= 9;
    bool rain = code >= 10 && code <= 19;
    bool snow = code >= 20 && code <= 25;
    bool crescent = code == 1 || code == 3 || code == 5 || code == 7;
    if (sun) {
        display_circle(x + 21, y + 21, 12, 0xfdc5);
        if (crescent) {
            display_circle(x + 27, y + 15, 11, 0x0000);
        } else {
            display_rect(x + 20, y + 3, 2, 4, 0xfdc5);
            display_rect(x + 20, y + 35, 2, 4, 0xfdc5);
            display_rect(x + 3, y + 20, 4, 2, 0xfdc5);
            display_rect(x + 35, y + 20, 4, 2, 0xfdc5);
        }
        return;
    }

    if (code == 4 || code == 5 || code == 7) {
        display_circle(x + 13, y + 11, 8, 0xfdc5);
        if (crescent) display_circle(x + 17, y + 8, 7, 0x0000);
    }
    if (cloudy || rain || snow || code >= 26) {
        int cy = y + (rain || snow ? 17 : 23);
        display_circle(x + 11, cy, 8, 0xe73e);
        display_circle(x + 22, cy - 4, 10, 0xe73e);
        display_circle(x + 32, cy + 1, 7, 0xe73e);
        display_rect(x + 11, cy, 22, 9, 0xe73e);
    }
    if (rain || snow) {
        for (int i = 0; i < 3; ++i) {
            int px = x + 11 + i * 10;
            if (rain) {
                display_rect(px, y + 29, 2, 4, 0x5e7f);
                display_rect(px - 1, y + 33, 2, 4, 0x5e7f);
            } else {
                display_circle(px, y + 32 + i % 2 * 3, 2, 0xffff);
            }
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

static void weather_temperature(int shift, int y, const char *label, int temperature, uint16_t ink)
{
    char number[20];
    snprintf(number, sizeof(number), "%d℃", temperature);
    display_utf8(shift + 234, y, label, ink);
    display_utf8(shift + 312 - display_utf8_width(number), y, number, ink);
}

static void weather_page(int page, int shift, const unsigned selected[3], const weather_status_t *weather,
                         const struct tm *local, bool has_time, uint16_t accent)
{
    if (page == 0 && !weather->current_valid) {
        shifted_center(shift, WEATHER_TOP + 14, weather_message(weather->error), 0xffff);
        return;
    }
    const weather_day_t *forecast = page ? &weather->forecast[selected[page - 1]] : NULL;
    const char *description = forecast ? forecast->text : weather->text;
    weather_icon(shift + 21, WEATHER_TOP - 5, forecast ? forecast->code : weather->code);
    // Seniverse's longest Chinese condition is seven glyphs (119 px at 1x).
    int scale = display_utf8_width(description) <= 70 ? 2 : 1;
    display_utf8_box(shift + 90, WEATHER_TOP - 2, 140, 40, description, scale, 0xffff);
    char label[32], location[128];
    time_t now = time(NULL);
    bool stale = !has_time || (forecast ? now < weather->daily_updated || now - weather->daily_updated > 21600 :
        now < weather->updated || now - weather->updated > 3600);
    if (forecast) {
        forecast_label(forecast->date, local, label, sizeof(label));
        weather_temperature(shift, WEATHER_TEMPERATURE_TOP, "最高", forecast->high, 0xffff);
        weather_temperature(shift, WEATHER_TEMPERATURE_BOTTOM, "最低", forecast->low, 0xb596);
    } else {
        strlcpy(label, "实况", sizeof(label));
        weather_temperature(shift, WEATHER_TEMPERATURE_TOP, "室外", weather->temperature, 0xffff);
        char today[11];
        strftime(today, sizeof(today), "%Y-%m-%d", local);
        if (has_time && weather->daily_valid && !strcmp(today, weather->daily_date)) {
            char range[32];
            snprintf(range, sizeof(range), "%d° / %d°", weather->low, weather->high);
            display_utf8(shift + 312 - display_utf8_width(range), WEATHER_TEMPERATURE_BOTTOM, range, 0xb596);
        }
    }
    snprintf(location, sizeof(location), "%s%s", weather->city, stale ? " 待更新" : "");
    display_utf8_box(shift + 4, WEATHER_TOP + 33, 76, 20, label, 1, 0xb596);
    display_utf8_box(shift + 90, WEATHER_TOP + 33, 140, 20, location, 1, stale ? accent : 0xb596);
}

static uint16_t comet_ink(float temperature)
{
    static const struct { float temperature; uint8_t red, green, blue; } palette[] = {
        {16.0f, 52, 108, 255}, {25.0f, 45, 219, 237},
        {40.0f, 255, 177, 52}, {50.0f, 255, 48, 48},
    };
    unsigned i = 0;
    while (i + 2 < sizeof(palette) / sizeof(palette[0]) &&
           temperature > palette[i + 1].temperature) ++i;
    float fraction = (temperature - palette[i].temperature) /
                     (palette[i + 1].temperature - palette[i].temperature);
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    unsigned red = palette[i].red + (palette[i + 1].red - palette[i].red) * fraction;
    unsigned green = palette[i].green + (palette[i + 1].green - palette[i].green) * fraction;
    unsigned blue = palette[i].blue + (palette[i + 1].blue - palette[i].blue) * fraction;
    return ((red & 0xf8) << 8) | ((green & 0xfc) << 3) | (blue >> 3);
}

static float comet_ease(int64_t elapsed)
{
    if (elapsed <= 0) return 0.0f;
    if (elapsed >= 1000) return 1.0f;
    float t = elapsed / 1000.0f;
    return t * t * (3.0f - 2.0f * t);
}

static void draw_temperature(const case_environment_t *environment, int64_t millis)
{
    enum { SWEEP_WIDTH = 278, SWEEP_MS = 8000, FADE_MS = 1200 };
    static struct { uint16_t ink; uint8_t y; } trail[SWEEP_WIDTH + 1];
    static bool active;
    static int last_pixel;
    static int64_t sample_time, move_started, sweep_started;
    static float reference, start_y, target_y, start_temperature, target_temperature;

    char label[80];
    if (!environment->valid) {
        snprintf(label, sizeof(label), "机箱温度：%s",
                 environment->error == ESP_ERR_NOT_FINISHED ? "读取中" : "传感器异常");
        center(159, label, 0x9cf3);
        active = false;
        return;
    }

    float temperature = environment->temperature;
    snprintf(label, sizeof(label), "机箱 %.1f℃  湿度 %.0f%%", temperature, environment->humidity);
    center(159, label, comet_ink(temperature));
    if (!active) {
        active = true;
        sample_time = environment->sampled_at;
        move_started = millis;
        sweep_started = millis;
        last_pixel = -1;
        reference = start_temperature = target_temperature = temperature;
        start_y = target_y = 130.0f;
    } else if (environment->sampled_at != sample_time) {
        float progress = comet_ease(millis - move_started);
        start_y += (target_y - start_y) * progress;
        start_temperature += (target_temperature - start_temperature) * progress;
        sample_time = environment->sampled_at;
        move_started = millis;
        target_temperature = temperature;
        // Keep a narrow vertical window, shifting its reference only at the edges.
        const float half_span = 1.2f;
        if (temperature > reference + half_span) reference = temperature - half_span;
        if (temperature < reference - half_span) reference = temperature + half_span;
        target_y = 130.0f - (temperature - reference) * 18.0f / half_span;
    }

    const int left = 20;
    int elapsed = millis - sweep_started;
    if (elapsed >= SWEEP_MS + FADE_MS) {
        sweep_started = millis;
        elapsed = 0;
        last_pixel = -1;
    }
    int head = elapsed >= SWEEP_MS ? SWEEP_WIDTH : elapsed * SWEEP_WIDTH / SWEEP_MS;
    unsigned fade = elapsed <= SWEEP_MS ? 255 : 255 * (SWEEP_MS + FADE_MS - elapsed) / FADE_MS;
    float progress = comet_ease(millis - move_started);
    int head_y = (int)(start_y + (target_y - start_y) * progress + 0.5f);
    uint16_t head_ink = comet_ink(start_temperature + (target_temperature - start_temperature) * progress);
    if (elapsed < SWEEP_MS || last_pixel < head) {
        for (int i = last_pixel + 1; i <= head; ++i) {
            trail[i].y = head_y;
            trail[i].ink = head_ink;
        }
        last_pixel = head;
    }
    for (int i = 0; i <= head; ++i) {
        int x = left + i;
        int y = trail[i].y;
        uint16_t ink = trail[i].ink;
        int strength = SWEEP_WIDTH - (head - i);
        unsigned opacity = (36 + 204 * strength * strength / (SWEEP_WIDTH * SWEEP_WIDTH)) * fade / 255;
        display_blend_rect(x, y - 2, 1, 5, ink, opacity / 3);
        display_blend_rect(x, y, 1, 2, ink, opacity);
    }
    if (elapsed < SWEEP_MS) {
        display_circle(left + head, head_y, 4, head_ink);
        display_circle(left + head, head_y, 2, 0xffff);
    }
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
    case_environment_t environment;
    case_environment_status(&environment);
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
    if (!clock_changed && !transition && !last_transition && !memcmp(&weather, &last_weather, sizeof(weather))) {
        display_rect(0, TEMPERATURE_TOP, 320, TEMPERATURE_BOTTOM - TEMPERATURE_TOP, 0x0000);
        draw_temperature(&environment, millis);
        display_present_region(TEMPERATURE_TOP, TEMPERATURE_BOTTOM - TEMPERATURE_TOP);
        return;
    }
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
        display_utf8(20, 7, text, 0xffff);
        snprintf(text, sizeof(text), "星期%s", weekdays[local.tm_wday]);
        display_utf8(300 - display_utf8_width(text), 7, text, 0xffff);
    } else center(7, "正在校准时间", 0xffff);
    if (has_time) strftime(text, sizeof(text), "%H", &local);
    else snprintf(text, sizeof(text), "--");
    display_text(20, 29, text, 10, 0xffff);
    display_rect(140, 49, 10, 10, 0xffff);
    display_rect(140, 79, 10, 10, 0xffff);
    if (has_time) strftime(text, sizeof(text), "%M", &local);
    else snprintf(text, sizeof(text), "--");
    display_text(160, 29, text, 10, 0xffff);
    if (has_time) strftime(text, sizeof(text), "%S", &local);
    else snprintf(text, sizeof(text), "--");
    display_text(278, 85, text, 2, accent);
    draw_temperature(&environment, millis);
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
    else display_present_region(TEMPERATURE_TOP, 240 - TEMPERATURE_TOP);
    last_time = now;
    last_weather = weather;
    last_transition = transition;
}
