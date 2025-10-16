#include "wifi_provisioning.h"
#include "esp_log.h"

static const char *TAG = "wifi_prov_new";

// Thin wrapper used in CMake to keep same SRCS layout between projects
void wifi_provisioning_new_start(void) {
    ESP_LOGI(TAG, "Starting WiFi provisioning (wrapper)");
    wifi_provisioning_init();
}
