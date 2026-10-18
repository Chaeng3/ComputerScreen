#include <math.h>
#include "display.h"
#include "scene.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

extern const uint16_t day_pixels[] asm("_binary_theme_day_rgb565_start");
extern const uint16_t dusk_pixels[] asm("_binary_theme_dusk_rgb565_start");
extern const uint16_t night_pixels[] asm("_binary_theme_night_rgb565_start");
static portMUX_TYPE preview_lock = portMUX_INITIALIZER_UNLOCKED;
static int preview_theme = -1;
static int64_t preview_until;

void scene_preview(int theme)
{
    int64_t until = esp_timer_get_time() / 1000 + 120000;
    portENTER_CRITICAL(&preview_lock);
    preview_theme = theme;
    preview_until = until;
    portEXIT_CRITICAL(&preview_lock);
}

scene_theme_t scene_effective_theme(scene_theme_t scheduled, int64_t millis)
{
    portENTER_CRITICAL(&preview_lock);
    int theme = preview_theme;
    int64_t until = preview_until;
    portEXIT_CRITICAL(&preview_lock);
    return theme >= SCENE_DAY && theme <= SCENE_NIGHT && millis < until ? (scene_theme_t)theme : scheduled;
}

static void glints(scene_theme_t theme, float seconds)
{
    // Deterministic positions keep the scene calm instead of generating visual noise.
    for (int i = 0; i < 12; ++i) {
        float light = sinf(seconds * 1.1f + i * 2.39f);
        if (light < 0.35f) continue;
        int x = 98 + (i * 43) % 128;
        int y = 161 + (i * 17) % 51;
        unsigned alpha = (unsigned)((light - 0.35f) * 130);
        display_blend_rect(x, y, 2 + i % 4, 1, theme == SCENE_DUSK ? 0xfdd5 : 0xcfff, alpha);
    }
}

void scene_draw(scene_theme_t theme, int64_t millis)
{
    const uint16_t *source = theme == SCENE_DAY ? day_pixels : theme == SCENE_DUSK ? dusk_pixels : night_pixels;
    float seconds = (millis % 3600000) / 1000.0f;
    for (int y = 0; y < 240; ++y) {
        int shift = 0, left = 0, right = 320;
        if (theme == SCENE_DAY && y >= 30 && y < 130) {
            // Fade the displacement at the cloud band's boundaries.
            float taper = sinf((y - 30) * 3.14159265f / 100);
            shift = (int)roundf(2 * taper * sinf(seconds * 0.12f));
        } else if (y >= 160 && y < 211) {
            left = 90; right = 235;
            shift = (int)roundf(sinf(seconds * 0.9f + y * 0.45f));
        }
        display_background_row(y, source + y * 320, shift, left, right);
    }
    glints(theme, seconds);
    if (theme == SCENE_NIGHT) {
        const int stars[][2] = {{24,39},{69,48},{108,20},{204,35},{270,22},{299,54},{287,96}};
        for (unsigned i = 0; i < sizeof(stars) / sizeof(stars[0]); ++i) {
            unsigned alpha = (unsigned)(90 + 80 * sinf(seconds * 0.8f + i * 1.7f));
            display_blend_rect(stars[i][0], stars[i][1], 1, 1, 0xffff, alpha);
        }
        int phase = millis % 37000;
        if (phase < 1400) {
            int x = 269 - phase / 15, y = 26 + phase / 46;
            for (int i = 0; i < 13; ++i)
                display_blend_rect(x + i, y - i / 3, 1, 1, 0xe7ff, (13 - i) * 14);
        }
    }
    // A continuous tint and tiny glyph shadows preserve the painting and readability.
    unsigned shade = theme == SCENE_DAY ? 78 : theme == SCENE_DUSK ? 55 : 20;
    display_blend_rect(0, 0, 320, 240, 0x0001, shade);
    for (int y = 153; y < 240; ++y) {
        unsigned alpha = (y < 168 ? (y - 153) * 4 : y > 225 ? (240 - y) * 4 : 60);
        display_blend_rect(0, y, 320, 1, 0x0001, alpha);
    }
}
