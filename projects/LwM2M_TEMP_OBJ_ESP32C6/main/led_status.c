#include "led_status.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

#if CONFIG_BOARD_HAS_WS2812
#include "led_strip.h"
#endif

static const char* TAG = "LED_STATUS";

static led_mode_t s_mode = LED_MODE_OFF;

#if CONFIG_BOARD_HAS_WS2812
static led_strip_handle_t s_strip = NULL;
#endif

static TaskHandle_t s_anim_task = NULL;

static inline void set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
#if CONFIG_BOARD_HAS_WS2812
    if (!s_strip) return;
    const uint8_t limit = 96; // cap brightness to ~38% for better visibility
    if (r > limit) r = limit;
    if (g > limit) g = limit;
    if (b > limit) b = limit;
    
    esp_err_t err = led_strip_set_pixel(s_strip, 0, r, g, b);
    if (err != ESP_OK) {
        // Silently ignore - not critical
        return;
    }
    
    err = led_strip_refresh(s_strip);
    if (err != ESP_OK) {
        // RMT channel busy - skip this update
        return;
    }
#else
    (void)r; (void)g; (void)b;
#endif
}

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
    
    // WiFi modes (DISABLED - Using Thread only)
    /*
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
    */
    
    // OpenThread states (patrón oficial Espressif)
    case LED_MODE_THREAD_DISABLED:
        // off (ya está en 0)
        break;
    case LED_MODE_THREAD_DETACHED:
        if (r) *r = 255; // red
        break;
    case LED_MODE_THREAD_CHILD:
        if (g) *g = 255; // green
        break;
    case LED_MODE_THREAD_ROUTER:
        if (r) *r = 255; // yellow
        if (g) *g = 255;
        break;
    case LED_MODE_THREAD_LEADER:
        if (r) *r = 255; // magenta/pink
        if (b) *b = 255;
        break;
    case LED_MODE_THREAD_JOINING:
        if (g) *g = 255; // cyan
        if (b) *b = 255;
        break;
    default: break;
    }
}

static void animator_task(void* arg)
{
    (void)arg;
    static int val = 0;
    static int dir = 1;
    static int blink_counter = 0;
    
    for (;;) {
        switch (s_mode) {
        case LED_MODE_OFF:
        case LED_MODE_THREAD_DISABLED:
            set_rgb(0,0,0);
            vTaskDelay(pdMS_TO_TICKS(200));
            break;
            
        case LED_MODE_FACTORY_RESET: {
            static bool on = false; on = !on;
            set_rgb(on ? 255 : 0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        }
        
        // WiFi modes (DISABLED - Using Thread only)
        /*
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
        */
        
        // OpenThread role states (patrón oficial Espressif)
        case LED_MODE_THREAD_DETACHED: {
            // red slow blink (1 Hz)
            blink_counter++;
            bool on = (blink_counter % 20) < 10; // 500ms on, 500ms off
            set_rgb(on ? 255 : 0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(50));
            break;
        }
        
        case LED_MODE_THREAD_CHILD:
            // solid green
            set_rgb(0, 255, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
            break;
            
        case LED_MODE_THREAD_ROUTER:
            // solid yellow
            set_rgb(255, 255, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
            break;
            
        case LED_MODE_THREAD_LEADER:
            // solid magenta (pink)
            set_rgb(255, 0, 255);
            vTaskDelay(pdMS_TO_TICKS(200));
            break;
            
        case LED_MODE_THREAD_JOINING: {
            // cyan fast blink (5 Hz)
            blink_counter++;
            bool on_join = (blink_counter % 4) < 2; // 100ms on, 100ms off
            set_rgb(0, on_join ? 255 : 0, on_join ? 255 : 0);
            vTaskDelay(pdMS_TO_TICKS(50));
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
            // Non-critical: LED not essential for Thread/LwM2M operation
            ESP_LOGW(TAG, "LED strip init failed (non-critical): %s", esp_err_to_name(err));
            s_strip = NULL;
            return;
        }
        // Give RMT channel time to fully initialize
        vTaskDelay(pdMS_TO_TICKS(50));
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

void led_status_set_thread_role(const char* role)
{
    if (!role) {
        led_status_set_mode(LED_MODE_THREAD_DISABLED);
        return;
    }
    
    // Patrón oficial Espressif para Thread LED status
    if (strcmp(role, "disabled") == 0) {
        led_status_set_mode(LED_MODE_THREAD_DISABLED);
        ESP_LOGI(TAG, "Thread LED: DISABLED (off)");
    } else if (strcmp(role, "detached") == 0) {
        led_status_set_mode(LED_MODE_THREAD_DETACHED);
        ESP_LOGI(TAG, "Thread LED: DETACHED (red slow blink)");
    } else if (strcmp(role, "child") == 0) {
        led_status_set_mode(LED_MODE_THREAD_CHILD);
        ESP_LOGI(TAG, "Thread LED: CHILD (green)");
    } else if (strcmp(role, "router") == 0) {
        led_status_set_mode(LED_MODE_THREAD_ROUTER);
        ESP_LOGI(TAG, "Thread LED: ROUTER (yellow)");
    } else if (strcmp(role, "leader") == 0) {
        led_status_set_mode(LED_MODE_THREAD_LEADER);
        ESP_LOGI(TAG, "Thread LED: LEADER (magenta)");
    } else {
        ESP_LOGW(TAG, "Unknown Thread role: %s", role);
    }
}
