#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "mqtt_client.h"

static const char *TAG = "TB_NODE";

// Config desde menuconfig
#define CFG_APP_USE_ETHERNET CONFIG_APP_USE_ETHERNET
#define WIFI_SSID CONFIG_MQTT_WIFI_SSID
#define WIFI_PASS CONFIG_MQTT_WIFI_PASS
#define TB_HOST   CONFIG_TB_HOST
#define TB_PORT   CONFIG_TB_PORT
#define TB_TOKEN  CONFIG_TB_TOKEN

static EventGroupHandle_t s_net_event_group;
static const int NET_CONNECTED_BIT = BIT0;
static volatile bool s_mqtt_connected = false;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        esp_wifi_connect();
        xEventGroupClearBits(s_net_event_group, NET_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got IP");
        xEventGroupSetBits(s_net_event_group, NET_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    if (!s_net_event_group) s_net_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_net_event_group, NET_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
    if (!(bits & NET_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "WiFi connection timeout");
    }
}

#if CONFIG_APP_USE_ETHERNET
static void ethernet_init(void) {
    if (!s_net_event_group) s_net_event_group = xEventGroupCreate();
    xEventGroupSetBits(s_net_event_group, NET_CONNECTED_BIT);
    ESP_LOGW(TAG, "Ethernet placeholder (QEMU bridge)");
}
#endif

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            s_mqtt_connected = true;
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            esp_mqtt_client_subscribe(event->client, "v1/devices/me/rpc/request/+", 1);
            esp_mqtt_client_subscribe(event->client, "v1/devices/me/attributes", 1);
            esp_mqtt_client_publish(event->client, "v1/devices/me/attributes", "{\"fw\":\"idf-node\"}", 0, 1, 0);
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_mqtt_connected = false;
            ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_DATA: {
            ESP_LOGI(TAG, "MQTT_EVENT_DATA topic=%.*s data=%.*s", event->topic_len, event->topic, event->data_len, event->data);
            if (event->topic_len > 0 && strncmp(event->topic, "v1/devices/me/rpc/request/", 26) == 0) {
                const char *req_id = event->topic + 26;
                char resp_topic[64];
                snprintf(resp_topic, sizeof(resp_topic), "v1/devices/me/rpc/response/%s", req_id);
                esp_mqtt_client_publish(event->client, resp_topic, event->data, event->data_len, 1, 0);
            }
            break;
        }
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGD(TAG, "MQTT event id=%ld", (long)event_id);
            break;
    }
}

static void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname = TB_HOST,
        .broker.address.port = TB_PORT,
        .credentials.username = TB_TOKEN,
        .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);

    while (1) {
        char payload[128];
        int temp = 24 + (esp_random() % 100) / 10;
        int hum = 60 + (esp_random() % 50) / 10;
        snprintf(payload, sizeof(payload), "{\"temperature\": %d, \"humidity\": %d}", temp, hum);

        if (s_mqtt_connected) {
            int msg_id = esp_mqtt_client_publish(client, "v1/devices/me/telemetry", payload, 0, 1, 0);
            ESP_LOGI(TAG, "Publish via MQTT (msg_id=%d): %s", msg_id, payload);
        } else {
            ESP_LOGW(TAG, "MQTT not connected; using bridge only");
        }
        printf("TBTELEMETRY:%s\n", payload);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
#if CONFIG_APP_USE_ETHERNET
    ethernet_init();
#else
    wifi_init_sta();
#endif
    ESP_LOGI(TAG, "TB Node starting");
    mqtt_app_start();
}
