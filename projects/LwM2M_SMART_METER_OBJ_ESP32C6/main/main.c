#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_sleep.h"

// Local modules
#include "wifi_provisioning.h"
#include "led_status.h"
#include "driver/gpio.h"

void lwm2m_client_start(void);

static const char *TAG = "lwm2m_main";

// On ESP32-C6, only RTC-capable (LP) GPIOs can wake from deep sleep. These map to GPIO0..GPIO7.
static inline bool is_deep_sleep_wake_capable_gpio(gpio_num_t gpio)
{
    return (gpio >= GPIO_NUM_0 && gpio <= GPIO_NUM_7);
}

static void factory_reset_task(void* arg)
{
    const gpio_num_t btn = (gpio_num_t)CONFIG_BOARD_BOOT_BUTTON_GPIO;
    const TickType_t hold_ticks = pdMS_TO_TICKS(CONFIG_FACTORY_RESET_HOLD_MS);
    const TickType_t poll = pdMS_TO_TICKS(20);

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << btn,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = true,
        .pull_down_en = false,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    TickType_t pressed_time = 0;
    bool was_pressed = false;

    for (;;) {
        bool pressed = (gpio_get_level(btn) == 0);
        if (pressed) {
            if (!was_pressed) {
                // Start counting, do not change LED yet
                pressed_time = 0;
                was_pressed = true;
            } else {
                pressed_time += poll;
                if (pressed_time >= hold_ticks) {
                    // Reached target hold time: show red blink and perform factory reset
                    led_status_set_mode(LED_MODE_FACTORY_RESET);
                    vTaskDelay(pdMS_TO_TICKS(600));
                    nvs_flash_deinit();
                    nvs_flash_erase();
                    // Optional: re-init not required before deep sleep
                    vTaskDelay(pdMS_TO_TICKS(50));
                    // Turn off LED before sleep (synchronous)
                    led_status_force_off();
                    vTaskDelay(pdMS_TO_TICKS(20));
                    // Configure deep-sleep input pulls to keep line high when not pressed
                    gpio_sleep_set_direction(btn, GPIO_MODE_INPUT);
                    gpio_sleep_set_pull_mode(btn, GPIO_PULLUP_ONLY);
                    // Enable wakeup on the same button (active-low) if RTC-capable
                    if (is_deep_sleep_wake_capable_gpio(btn)) {
                        esp_err_t werr = esp_sleep_enable_ext1_wakeup(1ULL << btn, ESP_EXT1_WAKEUP_ANY_LOW);
                        if (werr != ESP_OK) {
                            ESP_LOGW(TAG, "Failed to enable ext1 wake on GPIO %d: %s", btn, esp_err_to_name(werr));
                        } else {
                            ESP_LOGW(TAG, "Entering deep sleep after factory reset. Press BOOT to wake.");
                        }
                    } else {
                        ESP_LOGE(TAG, "GPIO %d cannot wake from deep sleep on ESP32-C6 (needs RTC GPIO 0..7). Use a valid RTC pin or RESET.", btn);
                        ESP_LOGW(TAG, "Entering deep sleep. Wake with RESET/EN or reconfigure wake GPIO to 0..7.");
                    }
                    // Start deep sleep
                    esp_deep_sleep_start();
                }
            }
        } else {
            if (was_pressed) {
                was_pressed = false;
            }
        }
        vTaskDelay(poll);
    }
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %d", ret);
        return;
    }

    // Initialize LED status and factory reset monitor first so LED shows provisioning state
    led_status_init();
    xTaskCreate(factory_reset_task, "factory_reset", 3072, NULL, 6, NULL);

    ESP_LOGI(TAG, "Starting WiFi Provisioning...");

    // Initialize WiFi provisioning system
    wifi_provisioning_init();

    // Wait for WiFi connection (either through provisioning or normal connection)
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    wifi_provisioning_wait_connected();

    ESP_LOGI(TAG, "WiFi connected! Starting LwM2M client...");

    // Start LwM2M client
    lwm2m_client_start();
}
