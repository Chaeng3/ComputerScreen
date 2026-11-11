#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool valid;
    float temperature;
    float humidity;
    esp_err_t error;
    int64_t sampled_at;
} case_environment_t;

void case_environment_init(void);
void case_environment_status(case_environment_t *status);
uint32_t case_temperature_color(float temperature);
unsigned case_sound_level(void);
unsigned case_sound_input(void);
