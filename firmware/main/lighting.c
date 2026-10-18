#include <time.h>
#include <stdatomic.h>
#include "lighting.h"
#include "lighting_plan.h"
#include "settings.h"
#include "network.h"
#include "display.h"
#include "esp_timer.h"
#include "esp_log.h"

static atomic_uint applied;
static unsigned last_target;
static int64_t last_step;

void lighting_init(void)
{
    const app_settings_t *s = settings_get();
    const lighting_settings_t *l = settings_get_lighting();
    applied = lighting_target(s->brightness, l->night_brightness, l->auto_dim,
                              s->day_start, s->night_start, 0, false);
    last_target = applied;
    display_brightness(applied);
}

unsigned lighting_current(void) { return atomic_load(&applied); }

void lighting_tick(void)
{
    int64_t tick = esp_timer_get_time();
    if (tick - last_step < 80000) return;
    last_step = tick;
    const app_settings_t *s = settings_get();
    const lighting_settings_t *l = settings_get_lighting();
    network_status_t net;
    network_status(&net);
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    unsigned target = lighting_target(s->brightness, l->night_brightness, l->auto_dim,
        s->day_start, s->night_start, local.tm_hour * 60 + local.tm_min,
        net.time_synced && local.tm_year >= 124);
    if (target != last_target) {
        ESP_LOGI("lighting", "Brightness target: %u%%", target);
        last_target = target;
    }
    if (applied != target) {
        applied = applied < target ? applied + 1 : applied - 1;
        display_brightness(applied);
    }
}
