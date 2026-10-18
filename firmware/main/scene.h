#pragma once
#include <stdint.h>

typedef enum { SCENE_DAY, SCENE_DUSK, SCENE_NIGHT } scene_theme_t;
void scene_preview(int theme); // -1 returns immediately to the schedule.
scene_theme_t scene_effective_theme(scene_theme_t scheduled, int64_t millis);
void scene_draw(scene_theme_t theme, int64_t millis);
