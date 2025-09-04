#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"

static const char *TAG = "HELLO";

#ifndef CONFIG_HELLO_BLINK_GPIO
#define CONFIG_HELLO_BLINK_GPIO GPIO_NUM_2  // On many boards, GPIO2 is wired to the on-board LED
#endif

void app_main(void)
{
    // Print basic chip info
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        ESP_LOGI(TAG, "ESP32 with %luMB flash", (unsigned long)(flash_size / (1024 * 1024)));
    }
    ESP_LOGI(TAG, "Cores: %d, Features: WIFI%s%s%s", chip_info.cores,
             (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "/EMB_FLASH" : "");

    // Configure LED GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << CONFIG_HELLO_BLINK_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    bool on = false;
    while (1) {
        gpio_set_level(CONFIG_HELLO_BLINK_GPIO, on);
        ESP_LOGI(TAG, "Hello from ESP32%s", on ? " (LED ON)" : " (LED OFF)");
        on = !on;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
