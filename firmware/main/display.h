#pragma once
#include <stdint.h>
#include <stdbool.h>
void display_init(void);
void display_brightness(unsigned percent);
void display_clear(uint16_t color);
void display_rect(int x, int y, int w, int h, uint16_t color);
void display_text(int x, int y, const char *text, int scale, uint16_t color);
void display_present(void);
void display_present_region(int y, int height);
int display_utf8_width(const char *text);
void display_utf8(int x, int y, const char *text, uint16_t color);
void display_utf8_box(int x, int y, int width, int height, const char *text, int scale, uint16_t color);
void display_circle(int x, int y, int radius, uint16_t color);
void display_text_shadow(bool enabled);
void display_background_row(int y, const uint16_t *wire_pixels, int shift, int left, int right);
void display_blend_rect(int x, int y, int w, int h, uint16_t color, unsigned alpha);
