#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "display.h"
#include "font_cn16_meta.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "board.h"

#define STRIP_LINES 16
static const char *TAG = "display";
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t transfer_done;
static uint16_t *pixels;
static uint16_t *frame;
static bool text_shadow;

static bool color_done(esp_lcd_panel_io_handle_t io,
                       esp_lcd_panel_io_event_data_t *event, void *ctx)
{
    (void)io;
    (void)event;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)ctx, &wake);
    return wake == pdTRUE;
}

static uint16_t wire_color(uint16_t rgb565)
{
    // ST7789 receives the most significant byte first.
    return (uint16_t)((rgb565 << 8) | (rgb565 >> 8));
}

static void fill_dma_rectangle(int x, int y, int width, int height, uint16_t color)
{
    assert(x >= 0 && y >= 0 && width > 0 && height > 0);
    assert(x + width <= LCD_WIDTH && y + height <= LCD_HEIGHT);
    for (int row = 0; row < height; row += STRIP_LINES) {
        int count = height - row;
        if (count > STRIP_LINES) count = STRIP_LINES;
        for (int i = 0; i < width * count; ++i) pixels[i] = wire_color(color);
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, x, y + row,
                                                 x + width, y + row + count, pixels));
        // The DMA buffer must remain unchanged until the callback arrives.
        if (xSemaphoreTake(transfer_done, pdMS_TO_TICKS(2000)) != pdTRUE) {
            ESP_LOGE(TAG, "LCD transfer timed out");
            abort();
        }
    }
}

static void backlight_init(void)
{
    ESP_ERROR_CHECK(gpio_reset_pin(LCD_PIN_BL));
    ESP_ERROR_CHECK(gpio_set_direction(LCD_PIN_BL, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(LCD_PIN_BL, 0));
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    const ledc_channel_config_t channel = {
        .gpio_num = LCD_PIN_BL, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .duty = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

void display_init(void)
{
    backlight_init();
    ESP_LOGI(TAG, "ST7789 landscape test: %dx%d; MOSI=%d CLK=%d CS=%d DC=%d RST=%d BL=%d",
             LCD_WIDTH, LCD_HEIGHT, LCD_PIN_MOSI, LCD_PIN_CLK, LCD_PIN_CS,
             LCD_PIN_DC, LCD_PIN_RST, LCD_PIN_BL);
    ESP_LOGI(TAG, "PSRAM available: %u bytes", (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    transfer_done = xSemaphoreCreateBinary();
    assert(transfer_done);
    pixels = heap_caps_malloc(LCD_WIDTH * STRIP_LINES * sizeof(uint16_t),
                              MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(pixels);
    frame = heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    assert(frame);
    const spi_bus_config_t bus = {
        .sclk_io_num = LCD_PIN_CLK, .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = -1, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * STRIP_LINES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_handle_t io;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_PIN_CS, .dc_gpio_num = LCD_PIN_DC,
        .spi_mode = 0, .pclk_hz = LCD_SPI_HZ, .trans_queue_depth = 1,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .on_color_trans_done = color_done, .user_ctx = transfer_done,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &io));
    const esp_lcd_panel_dev_config_t config = {
        .reset_gpio_num = LCD_PIN_RST, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, LCD_INVERT_COLORS));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, LCD_MIRROR_X, LCD_MIRROR_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    fill_dma_rectangle(0, 0, LCD_WIDTH, LCD_HEIGHT, 0);
}

void display_brightness(unsigned percent)
{
    if (percent > 100) percent = 100;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 1023 / 100));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}

void display_rect(int x, int y, int w, int h, uint16_t color)
{
    for (int row = y; row < y + h; ++row) {
        if (row < 0 || row >= LCD_HEIGHT) continue;
        for (int col = x; col < x + w; ++col) {
            if (col >= 0 && col < LCD_WIDTH) frame[row * LCD_WIDTH + col] = wire_color(color);
        }
    }
}

void display_clear(uint16_t color) { display_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, color); }

// Compact 5x7 diagnostic font: digits followed by uppercase letters.
static const uint8_t glyphs[][5] = {
    {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},{0x42,0x61,0x51,0x49,0x46},
    {0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},
    {6,0x49,0x49,0x29,0x1e},
    {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
    {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},
    {0x3e,0x41,0x49,0x49,0x7a},{0x7f,8,8,8,0x7f},{0,0x41,0x7f,0x41,0},
    {0x20,0x40,0x41,0x3f,1},{0x7f,8,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
    {0x7f,2,0x0c,2,0x7f},{0x7f,4,8,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
    {0x7f,9,9,9,6},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{1,1,0x7f,1,1},{0x3f,0x40,0x40,0x40,0x3f},
    {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,8,0x14,0x63},
    {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},
};

static void text_raw(int x, int y, const char *text, int scale, uint16_t color)
{
    while (*text) {
        unsigned char c = *text++;
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        uint8_t special[5] = {0};
        const uint8_t *glyph = special;
        if (c >= '0' && c <= '9') glyph = glyphs[c - '0'];
        else if (c >= 'A' && c <= 'Z') glyph = glyphs[10 + c - 'A'];
        else if (c == ':') special[2] = 0x24;
        else if (c == '.') special[2] = 0x40;
        else if (c == '-') memset(special, 8, 5);
        else if (c == '/') { special[0]=0x20; special[1]=0x10; special[2]=8; special[3]=4; special[4]=2; }
        for (int col = 0; col < 5; ++col) {
            for (int row = 0; row < 7; ++row) {
                if (glyph[col] & (1 << row)) display_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
        x += scale * 6;
    }
}

void display_text(int x, int y, const char *text, int scale, uint16_t color)
{
    if (text_shadow) text_raw(x + 1, y + 1, text, scale, 0);
    text_raw(x, y, text, scale, color);
}

void display_present_region(int start_y, int height)
{
    assert(start_y >= 0 && height > 0 && start_y + height <= LCD_HEIGHT);
    for (int y = start_y; y < start_y + height; y += STRIP_LINES) {
        int rows = start_y + height - y;
        if (rows > STRIP_LINES) rows = STRIP_LINES;
        memcpy(pixels, frame + y * LCD_WIDTH, LCD_WIDTH * rows * sizeof(uint16_t));
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, y, LCD_WIDTH, y + rows, pixels));
        if (xSemaphoreTake(transfer_done, pdMS_TO_TICKS(2000)) != pdTRUE) abort();
    }
}

void display_present(void) { display_present_region(0, LCD_HEIGHT); }

extern const uint8_t cn_font[] asm("_binary_font_cn16_bin_start");

static uint32_t next_codepoint(const char **text)
{
    const uint8_t *p = (const uint8_t *)*text;
    uint32_t c = *p++;
    if (c >= 0xc2 && c <= 0xdf && (p[0] & 0xc0) == 0x80) {
        c = ((c & 31) << 6) | (p[0] & 63); p++;
    } else if (c >= 0xe0 && c <= 0xef && p[0] && p[1] &&
               (p[0] & 0xc0) == 0x80 && (p[1] & 0xc0) == 0x80) {
        c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63); p += 2;
    } else if (c >= 0x80) c = '?';
    *text = (const char *)p;
    return c;
}

static unsigned font_index(uint32_t c)
{
    if (c >= 32 && c <= 126) return c - 32;
    if (c == 0xb0) return 95;
    if (c == 0x2103) return 96;
    if (c >= 0x4e00 && c <= 0x9fff) return 97 + c - 0x4e00;
    return '?' - 32;
}

int display_utf8_width(const char *text)
{
    int width = 0;
    while (*text) {
        unsigned index = font_index(next_codepoint(&text));
        width += index < 95 ? font_ascii_advance[index] : 17;
    }
    return width;
}

static void utf8_raw(int x, int y, const char *text, int scale, uint16_t color)
{
    while (*text && x < LCD_WIDTH) {
        unsigned index = font_index(next_codepoint(&text));
        const uint8_t *glyph = cn_font + index * 40;
        for (int row = 0; row < 20; ++row) {
            for (int col = 0; col < 16; ++col) {
                if (glyph[row * 2 + col / 8] & (0x80 >> (col % 8)))
                    display_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
        x += (index < 95 ? font_ascii_advance[index] : 17) * scale;
    }
}

void display_utf8(int x, int y, const char *text, uint16_t color)
{
    if (text_shadow) utf8_raw(x + 1, y + 1, text, 1, 0);
    utf8_raw(x, y, text, 1, color);
}

void display_utf8_box(int x, int y, int width, int height, const char *text, int scale, uint16_t color)
{
    if (scale < 1 || scale > 2 || width < 17 * scale || height < 20 * scale) return;
    const char *starts[4], *ends[4];
    int widths[4], count = 0, limit = height / (20 * scale);
    if (limit > 4) limit = 4;
    const char *p = text;
    while (*p && count < limit) {
        starts[count] = p;
        int used = 0;
        while (*p) {
            const char *next = p;
            unsigned index = font_index(next_codepoint(&next));
            int advance = (index < 95 ? font_ascii_advance[index] : 17) * scale;
            if (used + advance > width) break;
            used += advance;
            p = next;
        }
        ends[count] = p;
        widths[count++] = used;
    }
    bool clipped = *p != 0;
    int top = y + (height - count * 20 * scale) / 2;
    for (int i = 0; i < count; ++i) {
        char line[128];
        const char *end = ends[i];
        int used = widths[i];
        if (clipped && i == count - 1) {
            int dots = display_utf8_width("...") * scale;
            while (end > starts[i] && used + dots > width) {
                --end;
                while (end > starts[i] && ((unsigned char)*end & 0xc0) == 0x80) --end;
                const char *last = end;
                unsigned index = font_index(next_codepoint(&last));
                used -= (index < 95 ? font_ascii_advance[index] : 17) * scale;
            }
            used += dots;
        }
        size_t bytes = end - starts[i];
        if (bytes > sizeof(line) - 4) bytes = sizeof(line) - 4;
        memcpy(line, starts[i], bytes);
        line[bytes] = 0;
        if (clipped && i == count - 1) strcat(line, "...");
        utf8_raw(x + (width - used) / 2, top + i * 20 * scale, line, scale, color);
    }
}

void display_text_shadow(bool enabled) { text_shadow = enabled; }

void display_background_row(int y, const uint16_t *wire_pixels, int shift, int left, int right)
{
    assert(y >= 0 && y < LCD_HEIGHT && left >= 0 && right <= LCD_WIDTH && left <= right);
    uint16_t *destination = frame + y * LCD_WIDTH;
    memcpy(destination, wire_pixels, LCD_WIDTH * sizeof(uint16_t));
    if (shift) {
        for (int x = left; x < right; ++x) {
            int sample = x + shift;
            if (sample < 0) sample = 0;
            if (sample >= LCD_WIDTH) sample = LCD_WIDTH - 1;
            destination[x] = wire_pixels[sample];
        }
    }
}

void display_blend_rect(int x, int y, int w, int h, uint16_t color, unsigned alpha)
{
    if (alpha > 255) alpha = 255;
    unsigned inverse = 255 - alpha;
    for (int row = y; row < y + h; ++row) {
        if (row < 0 || row >= LCD_HEIGHT) continue;
        for (int col = x; col < x + w; ++col) {
            if (col < 0 || col >= LCD_WIDTH) continue;
            uint16_t old = wire_color(frame[row * LCD_WIDTH + col]);
            unsigned r = (((old >> 11) * inverse) + (color >> 11) * alpha) / 255;
            unsigned g = ((((old >> 5) & 63) * inverse) + ((color >> 5) & 63) * alpha) / 255;
            unsigned b = (((old & 31) * inverse) + (color & 31) * alpha) / 255;
            frame[row * LCD_WIDTH + col] = wire_color((r << 11) | (g << 5) | b);
        }
    }
}

void display_circle(int x, int y, int radius, uint16_t color)
{
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= radius * radius) display_rect(x + dx, y + dy, 1, 1, color);
        }
    }
}
