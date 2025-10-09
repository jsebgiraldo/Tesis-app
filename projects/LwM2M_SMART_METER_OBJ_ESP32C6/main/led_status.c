#include "led_status.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
// LED module removed intentionally

void led_status_get_color(uint8_t* r, uint8_t* g, uint8_t* b)
{
    if (r) {
        *r = 0;
    }
    if (g) {
        *g = 0;
    }
    if (b) {
        *b = 0;
    }
    switch (s_mode) {
    case LED_MODE_FACTORY_RESET:
        if (r) *r = 255;
        break;
    case LED_MODE_PROV_BLE:
        if (b) *b = 255;
        break;
    case LED_MODE_WIFI_CONNECTED:
        if (g) *g = 255;
        break;
    case LED_MODE_WIFI_FAIL:
        if (r) *r = 255;
        if (g) *g = 180; // amber-ish
        break;
    default: break;
    }
}

static void animator_task(void* arg)
{
    (void)arg;
    static int val = 0;
    static int dir = 1;
    for (;;) {
        switch (s_mode) {
        case LED_MODE_OFF:
            set_rgb(0,0,0);
            vTaskDelay(pdMS_TO_TICKS(200));
            break;
        case LED_MODE_FACTORY_RESET: {
            static bool on = false; on = !on;
            set_rgb(on ? 255 : 0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        }
        case LED_MODE_PROV_BLE:
            // blue breathing
            set_rgb(0, 0, (uint8_t)val);
            val += dir * 10;
            if (val >= 255) { val = 255; dir = -1; }
            if (val <= 0)   { val = 0;   dir =  1; }
            vTaskDelay(pdMS_TO_TICKS(25));
            break;
        case LED_MODE_WIFI_CONNECTED:
            set_rgb(0, 255, 0);
            vTaskDelay(pdMS_TO_TICKS(400));
            break;
        case LED_MODE_WIFI_FAIL: {
            static bool on2 = false; on2 = !on2;
            // amber blink 200ms
            set_rgb(on2 ? 255 : 0, on2 ? 180 : 0, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
            break;
        }
        }
    }
}

void led_status_set_mode(led_mode_t mode)
{
    s_mode = mode;
}

void led_status_init(void)
{
#if CONFIG_BOARD_HAS_WS2812
    if (!s_strip) {
        led_strip_config_t strip_cfg = {
            .strip_gpio_num = CONFIG_BOARD_WS2812_GPIO,
            .max_leds = 1,
            .led_model = LED_MODEL_WS2812,
            .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        };
        led_strip_rmt_config_t rmt_cfg = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 10 * 1000 * 1000,
            .mem_block_symbols = 0,
            .flags.with_dma = false,
        };
        esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "led_strip init failed: %s", esp_err_to_name(err));
            return;
        }
    }
#endif
    if (!s_anim_task) {
        xTaskCreatePinnedToCore(animator_task, "led_anim", 3072, NULL, 5, &s_anim_task, tskNO_AFFINITY);
    }
    set_rgb(0,0,0);
    ESP_LOGI(TAG, "LED status initialized (WS2812 GPIO %d)", CONFIG_BOARD_WS2812_GPIO);
}

void led_status_force_off(void)
{
    s_mode = LED_MODE_OFF;
    set_rgb(0,0,0);
}
