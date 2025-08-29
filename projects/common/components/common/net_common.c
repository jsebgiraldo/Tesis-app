#include "sdkconfig.h"
#include "net_common.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#if CONFIG_ESP_WIFI_ENABLED
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#endif
#if CONFIG_ETH_ENABLED && CONFIG_MQTT_USE_ETH
#include "esp_eth.h"
#endif

static const char *TAG = "net_common";
static esp_netif_t *s_wifi = NULL;
static esp_netif_t *s_eth = NULL;
static esp_netif_t *s_thread = NULL; // placeholder

void net_common_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(nvs_flash_init());
}

void net_common_create_interfaces(bool enable_wifi, bool enable_eth, bool enable_thread) {
#if CONFIG_ESP_WIFI_ENABLED
    if (enable_wifi && !s_wifi) {
        s_wifi = esp_netif_create_default_wifi_sta();
    }
#else
    if (enable_wifi) {
        ESP_LOGW(TAG, "WiFi no habilitado en configuración de IDF");
    }
#endif
#if CONFIG_ETH_ENABLED && CONFIG_MQTT_USE_ETH
    if (enable_eth && !s_eth) {
        s_eth = esp_netif_create_default_eth_netif();
    }
#else
    if (enable_eth) {
        ESP_LOGW(TAG, "Ethernet no habilitado en configuración de IDF o desactivado en opciones del proyecto");
    }
#endif
    if (enable_thread && !s_thread) {
        ESP_LOGW(TAG, "Thread habilitado pero no implementado en este SoC");
    }
}

esp_netif_t* net_common_get_netif(net_if_t ifx) {
    switch (ifx) {
        case NET_IF_WIFI_STA: return s_wifi;
        case NET_IF_ETH: return s_eth;
        case NET_IF_THREAD: return s_thread;
        default: return NULL;
    }
}

bool net_common_wait_ip(net_if_t ifx, int timeout_ms) {
    esp_netif_t *n = net_common_get_netif(ifx);
    if (!n) return false;
    for (int t = 0; t < timeout_ms/100; ++t) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(n, &ip) == ESP_OK && ip.ip.addr != 0) {
            ESP_LOGI(TAG, "IP obtenida: %s", ip4addr_ntoa((const ip4_addr_t*)&ip.ip));
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return false;
}
