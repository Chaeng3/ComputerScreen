#include <stdlib.h>
#include <stdatomic.h>
#include "case_environment.h"
#include "board.h"
#include "settings.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_private/esp_gpio_reserve.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "case_environment";
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static case_environment_t current = {.error = ESP_ERR_NOT_FINISHED};
static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t sensor;
#define EFFECT_PIXELS 5
typedef struct {
    rmt_channel_handle_t channel;
    rmt_encoder_handle_t encoder;
} led_output_t;
static led_output_t status_led, effect_led;
static atomic_uint sound_level;
static atomic_uint sound_input;

void case_environment_status(case_environment_t *status)
{
    portENTER_CRITICAL(&lock);
    *status = current;
    portEXIT_CRITICAL(&lock);
    if (status->valid && esp_timer_get_time() - status->sampled_at > 10000000) {
        status->valid = false;
        status->error = ESP_ERR_TIMEOUT;
    }
}

static esp_err_t led_init(led_output_t *output, int pin, unsigned memory)
{
    rmt_tx_channel_config_t config = {
        .gpio_num = pin, .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10000000, .mem_block_symbols = memory, .trans_queue_depth = 1,
    };
    esp_err_t err = rmt_new_tx_channel(&config, &output->channel);
    if (err != ESP_OK) return err;
    rmt_copy_encoder_config_t copy = {};
    err = rmt_new_copy_encoder(&copy, &output->encoder);
    if (err == ESP_OK) err = rmt_enable(output->channel);
    if (err != ESP_OK) {
        if (output->encoder) rmt_del_encoder(output->encoder);
        rmt_del_channel(output->channel);
        output->channel = NULL;
    }
    return err;
}

static uint32_t scale_rgb(uint32_t rgb, unsigned percent)
{
    return (((rgb >> 16) * percent / 100) << 16) |
           ((((rgb >> 8) & 255) * percent / 100) << 8) |
           ((rgb & 255) * percent / 100);
}

static void led_write(const led_output_t *output, const uint32_t *pixels, unsigned count)
{
    if (!output->channel) return;
    rmt_symbol_word_t symbols[EFFECT_PIXELS * 24 + 1];
    for (unsigned pixel = 0; pixel < count; ++pixel) {
        uint32_t rgb = pixels[pixel];
        uint32_t grb = ((rgb & 0xff00) << 8) | ((rgb & 0xff0000) >> 8) | (rgb & 255);
        for (unsigned bit = 0; bit < 24; ++bit) {
            bool one = grb & (1UL << (23 - bit));
            symbols[pixel * 24 + bit] = (rmt_symbol_word_t){
                .level0 = 1, .duration0 = one ? 9 : 4, .duration1 = one ? 4 : 9,
            };
        }
    }
    symbols[count * 24] = (rmt_symbol_word_t){.duration0 = 1500, .duration1 = 1500};
    rmt_transmit_config_t tx = {.loop_count = 0};
    esp_err_t err = rmt_transmit(output->channel, output->encoder, symbols,
                                 (count * 24 + 1) * sizeof(*symbols), &tx);
    if (err == ESP_OK) err = rmt_tx_wait_all_done(output->channel, -1);
    if (err != ESP_OK) ESP_LOGW(TAG, "RGB write: %s", esp_err_to_name(err));
}

uint32_t case_temperature_color(float temperature)
{
    if (temperature < 30.0f) return 0x4a94ff;
    if (temperature < 40.0f) return 0x30dcc9;
    if (temperature < 50.0f) return 0xffab30;
    return 0xff3030;
}

unsigned case_sound_level(void)
{
    return atomic_load(&sound_level);
}

unsigned case_sound_input(void)
{
    return atomic_load(&sound_input);
}

static esp_err_t sensor_init(void)
{
    // Do not remap a live memory signal. Report unavailable and keep the UI alive.
    uint64_t pins = (1ULL << AHT_PIN_SCL) | (1ULL << AHT_PIN_SDA);
    bool memory_pin = false;
#if CONFIG_SPIRAM_MODE_OCT
    memory_pin = esp_psram_is_initialized() && (pins & (0x1fULL << 33));
#endif
    if (memory_pin || esp_gpio_is_reserved(pins)) {
        ESP_LOGE(TAG, "AHT20 GPIO%d/%d reserved by system", AHT_PIN_SCL, AHT_PIN_SDA);
        return ESP_ERR_INVALID_STATE;
    }
    i2c_master_bus_config_t config = {
        .i2c_port = -1, .sda_io_num = AHT_PIN_SDA, .scl_io_num = AHT_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
    };
    esp_err_t err = i2c_new_master_bus(&config, &bus);
    if (err != ESP_OK) return err;
    i2c_device_config_t device = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x38,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(bus, &device, &sensor);
    if (err != ESP_OK) { i2c_del_master_bus(bus); bus = NULL; }
    return err;
}

static uint8_t crc8(const uint8_t *bytes, size_t count)
{
    uint8_t crc = 0xff;
    for (size_t i = 0; i < count; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1;
    }
    return crc;
}

static esp_err_t sample(case_environment_t *result)
{
    uint8_t status;
    esp_err_t err = i2c_master_receive(sensor, &status, 1, 100);
    if (err != ESP_OK) return err;
    if (status & 0x80) return ESP_ERR_NOT_FINISHED;
    if (!(status & 0x08)) {
        const uint8_t init[] = {0xbe, 0x08, 0x00};
        err = i2c_master_transmit(sensor, init, sizeof(init), 100);
        if (err != ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    const uint8_t trigger[] = {0xac, 0x33, 0x00};
    err = i2c_master_transmit(sensor, trigger, sizeof(trigger), 100);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(80));
    uint8_t data[7];
    for (int attempt = 0; attempt < 6; ++attempt) {
        err = i2c_master_receive(sensor, data, sizeof(data), 100);
        if (err != ESP_OK) return err;
        if (!(data[0] & 0x80)) break;
        if (attempt == 5) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!(data[0] & 0x08)) return ESP_ERR_INVALID_STATE;
    if (crc8(data, 6) != data[6]) return ESP_ERR_INVALID_CRC;
    uint32_t humidity = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | (data[3] >> 4);
    uint32_t temperature = ((uint32_t)(data[3] & 15) << 16) | ((uint32_t)data[4] << 8) | data[5];
    result->humidity = humidity * (100.0f / 1048576.0f);
    result->temperature = temperature * (200.0f / 1048576.0f) - 50.0f;
    result->sampled_at = esp_timer_get_time();
    result->valid = true;
    return ESP_OK;
}

static uint32_t rainbow(unsigned hue)
{
    hue %= 1536;
    unsigned rise = hue % 256;
    unsigned fall = 255 - rise;
    switch (hue / 256) {
        case 0: return 0xff0000 | (rise << 8);
        case 1: return (fall << 16) | 0xff00;
        case 2: return 0xff00 | rise;
        case 3: return (fall << 8) | 0xff;
        case 4: return (rise << 16) | 0xff;
        default: return 0xff0000 | fall;
    }
}

static i2s_chan_handle_t mic_open(void)
{
    i2s_chan_handle_t rx = NULL;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&channel, NULL, &rx);
    if (err == ESP_OK) {
        i2s_std_config_t config = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = MIC_PIN_SCK, .ws = MIC_PIN_WS,
                         .dout = I2S_GPIO_UNUSED, .din = MIC_PIN_SD},
        };
        config.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
        err = i2s_channel_init_std_mode(rx, &config);
        if (err == ESP_OK) err = i2s_channel_enable(rx);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Microphone initialization: %s", esp_err_to_name(err));
        if (rx) i2s_del_channel(rx);
        return NULL;
    }
    ESP_LOGI(TAG, "ICS-43434 enabled (left channel)");
    return rx;
}

static unsigned mic_level(i2s_chan_handle_t rx)
{
    int32_t samples[256];
    size_t bytes = 0;
    if (!rx || i2s_channel_read(rx, samples, sizeof(samples), &bytes, 25) != ESP_OK ||
        bytes < sizeof(*samples)) return 0;
    unsigned count = bytes / sizeof(*samples);
    int sum = 0;
    for (unsigned i = 0; i < count; ++i) sum += samples[i] >> 9;
    int average = sum / (int)count;
    unsigned magnitude = 0;
    for (unsigned i = 0; i < count; ++i) magnitude += abs((samples[i] >> 9) - average);
    return magnitude / count;
}

static void effects_task(void *arg)
{
    const unsigned quiet_reference = 24 * 128;
    const unsigned noise_floor = 3 * 128;
    i2s_chan_handle_t rx = NULL;
    unsigned previous_mode = RGB_MODE_COUNT;
    unsigned envelope = 0, reference = quiet_reference;
    int64_t mic_ready_at = 0;
    uint32_t previous_status = UINT32_MAX;
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        rgb_settings_t rgb = settings_get_rgb();
        case_environment_t environment;
        case_environment_status(&environment);
        uint32_t status = environment.valid ? scale_rgb(case_temperature_color(environment.temperature), 9) : 0;
        if (status != previous_status) {
            led_write(&status_led, &status, 1);
            previous_status = status;
        }
        if (rgb.mode != previous_mode) {
            if (rgb.mode == RGB_SOUND) {
                rx = mic_open();
                envelope = 0;
                reference = quiet_reference;
                mic_ready_at = esp_timer_get_time() + 300000;
            } else if (rx) {
                i2s_channel_disable(rx);
                i2s_del_channel(rx);
                rx = NULL;
            }
            previous_mode = rgb.mode;
        }

        unsigned volume = 0;
        unsigned magnitude = 0;
        if (rx) {
            magnitude = mic_level(rx);
            // Drain the startup transient without feeding it into the gain tracker.
            if (esp_timer_get_time() < mic_ready_at) magnitude = 0;
            envelope = envelope * 7 / 8;
            if (magnitude > envelope) envelope = magnitude;
            reference = reference * 15 / 16;
            if (reference < quiet_reference) reference = quiet_reference;
            if (reference < envelope) reference = envelope;
            if (envelope > noise_floor)
                volume = (envelope - noise_floor) * 100 / (reference - noise_floor);
        }
        atomic_store(&sound_input, magnitude);
        atomic_store(&sound_level, volume);

        int64_t now = esp_timer_get_time();
        uint32_t pixels[EFFECT_PIXELS] = {0};
        uint32_t color = rgb.color;
        unsigned brightness = rgb.brightness;
        switch (rgb.mode) {
            case RGB_SOLID:
                break;
            case RGB_RAINBOW:
                color = rainbow((now / 8000) % 1536);
                break;
            case RGB_BREATHE: {
                unsigned phase = (now / 20000) % 200;
                unsigned wave = phase <= 100 ? phase : 200 - phase;
                brightness = brightness * (10 + 90 * wave * wave / 10000) / 100;
                break;
            }
            case RGB_SOUND:
                break;
            default:
                brightness = 0;
                break;
        }
        uint32_t pixel = scale_rgb(color, brightness);
        if (rgb.mode == RGB_SOUND) pixel = scale_rgb(pixel, volume);
        for (unsigned i = 0; i < EFFECT_PIXELS; ++i) pixels[i] = pixel;
        led_write(&effect_led, pixels, EFFECT_PIXELS);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(30));
    }
}

static void environment_task(void *arg)
{
    esp_err_t init_error = sensor_init();
    esp_err_t previous_error = ESP_ERR_NOT_FINISHED;
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        case_environment_t result = {0};
        result.error = init_error == ESP_OK ? sample(&result) : init_error;
        portENTER_CRITICAL(&lock);
        current = result;
        portEXIT_CRITICAL(&lock);
        if (result.error != previous_error) {
            ESP_LOGI(TAG, "AHT20: %s", esp_err_to_name(result.error));
            previous_error = result.error;
        }
        if (result.valid) {
            ESP_LOGD(TAG, "Case %.1f C, %.1f %%RH", result.temperature, result.humidity);
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(2000));
    }
}

void case_environment_init(void)
{
    esp_err_t err = led_init(&status_led, STATUS_LED_PIN, 48);
    if (err != ESP_OK) ESP_LOGW(TAG, "GPIO38 RGB init: %s", esp_err_to_name(err));
    err = led_init(&effect_led, EFFECT_LED_PIN, 144);
    if (err != ESP_OK) ESP_LOGW(TAG, "GPIO7 RGB init: %s", esp_err_to_name(err));
    if ((status_led.channel || effect_led.channel) &&
        xTaskCreate(effects_task, "rgb_effects", 4096, NULL, 2, NULL) != pdPASS)
        ESP_LOGE(TAG, "Unable to start RGB task");
    if (xTaskCreate(environment_task, "case_environment", 4096, NULL, 3, NULL) != pdPASS) {
        portENTER_CRITICAL(&lock);
        current.error = ESP_ERR_NO_MEM;
        portEXIT_CRITICAL(&lock);
        ESP_LOGE(TAG, "Unable to start sensor task");
    }
}
