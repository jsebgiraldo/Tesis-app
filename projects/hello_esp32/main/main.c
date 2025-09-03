#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_system.h"
// Official ESP-IDF component for addressable LEDs (WS2812/NeoPixel) using RMT
#include "led_strip.h"

static const char *TAG = "HELLO";

#ifndef CONFIG_HELLO_BLINK_GPIO
#define CONFIG_HELLO_BLINK_GPIO GPIO_NUM_15
#endif

#ifndef CONFIG_HELLO_RGB_LED_GPIO
#define CONFIG_HELLO_RGB_LED_GPIO 8
#endif

#ifndef CONFIG_HELLO_RGB_LED_COUNT
#define CONFIG_HELLO_RGB_LED_COUNT 1
#endif

// Helpers: HSV->RGB (integer) and simple gamma correction (~2.0)
static void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v,
                       uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t region = (uint8_t)(h / 60);   // 0..5
    uint16_t f = (uint16_t)(h % 60) * 255 / 60; // 0..255
    uint16_t p = (uint16_t)v * (255 - s) / 255;
    uint16_t q = (uint16_t)v * (255 - (uint16_t)s * f / 255) / 255;
    uint16_t t = (uint16_t)v * (255 - (uint16_t)s * (255 - f) / 255) / 255;
    switch (region) {
        case 0: *r = v;        *g = (uint8_t)t; *b = (uint8_t)p; break;
        case 1: *r = (uint8_t)q;*g = v;        *b = (uint8_t)p; break;
        case 2: *r = (uint8_t)p;*g = v;        *b = (uint8_t)t; break;
        case 3: *r = (uint8_t)p;*g = (uint8_t)q;*b = v;        break;
        case 4: *r = (uint8_t)t;*g = (uint8_t)p;*b = v;        break;
        default:*r = v;        *g = (uint8_t)p; *b = (uint8_t)q; break;
    }
}

static inline uint8_t gamma8(uint8_t v)
{
    // Approx gamma 2.0: y = x^2 scaled back to 0..255
    uint16_t x = v;
    uint16_t y = (x * x + 255) / 255;
    if (y > 255) y = 255;
    return (uint8_t)y;
}

void app_main(void)
{
    // Basic info
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "Target: %s", CONFIG_IDF_TARGET);

    // Configure simple blink GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << CONFIG_HELLO_BLINK_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Setup RGB LED via led_strip (RMT backend)
    led_strip_handle_t strip = NULL;
    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_HELLO_RGB_LED_GPIO,
        .max_leds = CONFIG_HELLO_RGB_LED_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    if (led_strip_new_rmt_device(&strip_config, &rmt_config, &strip) != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo inicializar LED RGB; solo GPIO blink.");
        strip = NULL;
    } else {
        led_strip_clear(strip);
    }

    bool blink_on = false;
    uint16_t hue = 0;           // 0..359
    const uint8_t sat = 255;    // saturación total
    const uint8_t val = 48;     // brillo moderado
    uint32_t tick = 0;
    const TickType_t step_delay = pdMS_TO_TICKS(20); // ~50 FPS

    while (1) {
        // Blink por GPIO cada ~500ms sin bloquear la animación RGB
        if ((tick % 25) == 0) {
            blink_on = !blink_on;
            gpio_set_level(CONFIG_HELLO_BLINK_GPIO, blink_on);
        }

        if (strip) {
            uint8_t r = 0, g = 0, b = 0;
            hsv_to_rgb(hue, sat, val, &r, &g, &b);
            r = gamma8(r); g = gamma8(g); b = gamma8(b);
            led_strip_set_pixel(strip, 0, r, g, b);
            led_strip_refresh(strip);
            hue = (hue + 2) % 360; // paso pequeño para fade suave
        }

        if ((tick % 50) == 0) {
            ESP_LOGI(TAG, "Hello from %s%s", CONFIG_IDF_TARGET, blink_on ? " (LED ON)" : " (LED OFF)");
        }
        tick++;
        vTaskDelay(step_delay);
    }
}
