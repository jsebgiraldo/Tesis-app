#include "ethernet.h"
#include "esp_event.h"
#include "esp_eth.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "eth";
static EventGroupHandle_t s_eth_event_group;
static const int ETH_CONNECTED_BIT = BIT0;
static esp_eth_handle_t s_eth_handle = NULL;

static void eth_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data) {
    switch (event_id) {
        case ETHERNET_EVENT_CONNECTED:
            xEventGroupSetBits(s_eth_event_group, ETH_CONNECTED_BIT);
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            xEventGroupClearBits(s_eth_event_group, ETH_CONNECTED_BIT);
            break;
        default:
            break;
    }
}

esp_err_t eth_init_default(void) {
    s_eth_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));

    // Configuración genérica RMII: ajustar según placa (pines/PHY)
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

#if CONFIG_IDF_TARGET_ESP32
    // MAC para ESP32 EMAC interno
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&mac_config);
#elif CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C2
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&mac_config);
#else
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&mac_config);
#endif

    // PHY genérico IP101 por defecto (cambiar a TLK110, LAN8720, etc. si aplica)
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);

    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &s_eth_handle));

    return ESP_OK;
}

void eth_start(void) {
    if (s_eth_handle) {
        ESP_ERROR_CHECK(esp_eth_start(s_eth_handle));
    }
}

void eth_stop(void) {
    if (s_eth_handle) {
        ESP_ERROR_CHECK(esp_eth_stop(s_eth_handle));
    }
}

bool eth_is_connected(void) {
    EventBits_t bits = xEventGroupGetBits(s_eth_event_group);
    return (bits & ETH_CONNECTED_BIT) != 0;
}
