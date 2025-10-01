// Anjay client registering Device(3), Location(6) and Smart Meter (10243)
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include "esp_mac.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "device_object.h"
#include "firmware_update.h"
#include "location_object.h"
#include "smart_meter_object.h"

#include <anjay/anjay.h>
#include <anjay/security.h>
#include <anjay/server.h>
#include <avsystem/commons/avs_time.h>
#include <avsystem/commons/avs_log.h>
#include <anjay/attr_storage.h>
#include <avsystem/commons/avs_stream_membuf.h>
#include <avsystem/commons/avs_stream_inbuf.h>
#include "nvs.h"

static const char *TAG = "lwm2m_client";

#ifndef CONFIG_LWM2M_TASK_STACK_SIZE
#define CONFIG_LWM2M_TASK_STACK_SIZE 8192
#endif
#ifndef CONFIG_LWM2M_START_DELAY_MS
#define CONFIG_LWM2M_START_DELAY_MS 1000
#endif
#ifndef CONFIG_LWM2M_IN_BUFFER_SIZE
#define CONFIG_LWM2M_IN_BUFFER_SIZE 4000
#endif
#ifndef CONFIG_LWM2M_OUT_BUFFER_SIZE
#define CONFIG_LWM2M_OUT_BUFFER_SIZE 4000
#endif
#ifndef CONFIG_LWM2M_MSG_CACHE_SIZE
#define CONFIG_LWM2M_MSG_CACHE_SIZE 4000
#endif
#ifndef CONFIG_LWM2M_BOOTSTRAP
#define CONFIG_LWM2M_BOOTSTRAP 0
#endif
#ifndef CONFIG_LWM2M_SERVER_SHORT_ID
#define CONFIG_LWM2M_SERVER_SHORT_ID 123
#endif
#ifndef CONFIG_LWM2M_SECURITY_PSK_ID
#define CONFIG_LWM2M_SECURITY_PSK_ID ""
#endif
#ifndef CONFIG_LWM2M_SECURITY_PSK_KEY
#define CONFIG_LWM2M_SECURITY_PSK_KEY ""
#endif

static char g_endpoint_name[64] = "";

static const char *resolve_endpoint_name(void) {
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        (void) esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    }
    snprintf(g_endpoint_name, sizeof(g_endpoint_name),
             "ESP32C6-SM-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return g_endpoint_name;
}

// Try to obtain the default gateway IPv4 address as a string.
// Returns true on success and writes into ip_out.
static bool get_gateway_ipv4(char *ip_out, size_t ip_out_size) {
    if (!ip_out || ip_out_size < 8) {
        return false;
    }
    esp_netif_t *netif = esp_netif_get_default_netif();
    if (!netif) {
        return false;
    }
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        return false;
    }
    struct in_addr ina;
    ina.s_addr = ip_info.gw.addr; // IPv4 gateway
    const char *s = inet_ntop(AF_INET, &ina, ip_out, (socklen_t) ip_out_size);
    return (s && *s);
}

static bool resolve_hostname_ipv4(const char *hostname, char *ip_out, size_t ip_out_size) {
    if (!hostname || !*hostname || !ip_out || ip_out_size < 8) {
        return false;
    }
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo *res = NULL;
    int err = getaddrinfo(hostname, NULL, &hints, &res);
    if (err != 0 || !res) {
        ESP_LOGW(TAG, "getaddrinfo('%s') failed: %d", hostname, err);
        return false;
    }
    bool ok = false;
    for (struct addrinfo *p = res; p; p = p->ai_next) {
        if (p->ai_family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *) p->ai_addr;
            const char *s = inet_ntop(AF_INET, &addr->sin_addr, ip_out, (socklen_t) ip_out_size);
            if (s && *s) { ok = true; break; }
        }
    }
    freeaddrinfo(res);
    return ok;
}

static void build_final_server_uri(char *out, size_t out_size) {
    const bool is_secure =
#if CONFIG_LWM2M_SERVER_SCHEME_COAPS
        true;
#else
        false;
#endif
    int port = (int) CONFIG_LWM2M_SERVER_PORT;
    char resolved_ip[48] = {0};
    const char *host = NULL;

    const char *configured_host = CONFIG_LWM2M_OVERRIDE_HOSTNAME;
    if (configured_host && configured_host[0] && 0) {
        if (resolve_hostname_ipv4(configured_host, resolved_ip, sizeof(resolved_ip))) {
            host = resolved_ip;
        } else {
            char gw[32] = {0};
            if (get_gateway_ipv4(gw, sizeof(gw))) {
                ESP_LOGW(TAG, "DNS resolve failed for '%s', falling back to gateway %s", configured_host, gw);
                host = gw;
            } else {
                ESP_LOGW(TAG, "DNS resolve failed for '%s', and no gateway available; using as-is", configured_host);
                host = configured_host;
            }
        }
    } else {
        host = "192.168.3.100";
    }
    snprintf(out, out_size, "%s://%s:%d", is_secure ? "coaps" : "coap", host, port);
}

static int setup_security(anjay_t *anjay) {
    anjay_security_object_purge(anjay);
    anjay_security_instance_t sec = {
        .ssid = CONFIG_LWM2M_SERVER_SHORT_ID,
        .security_mode = ANJAY_SECURITY_NOSEC,
    };
#if CONFIG_LWM2M_BOOTSTRAP
    sec.bootstrap_server = true;
    sec.server_uri = CONFIG_LWM2M_BOOTSTRAP_URI;
#else
    sec.bootstrap_server = false;
    sec.ssid = CONFIG_LWM2M_SERVER_SHORT_ID;
    static char server_uri_buf[160];
    build_final_server_uri(server_uri_buf, sizeof(server_uri_buf));
    sec.server_uri = server_uri_buf;
#endif
    const char *uri = sec.server_uri;
    bool uri_secure = (uri && strncmp(uri, "coaps", 5) == 0);
    const char *psk_id = (CONFIG_LWM2M_SECURITY_PSK_ID[0] != '\0') ? CONFIG_LWM2M_SECURITY_PSK_ID : g_endpoint_name;
    const char *psk_key_hex = CONFIG_LWM2M_SECURITY_PSK_KEY;
    if (uri_secure && psk_id && psk_key_hex && psk_id[0] != '\0' && psk_key_hex[0] != '\0') {
        uint8_t key_buf[64];
        size_t len = 0;
        size_t hexlen = strlen(psk_key_hex);
        if (hexlen % 2 == 0 && hexlen/2 <= sizeof(key_buf)) {
            for (size_t i = 0; i < hexlen/2; ++i) {
                char c1 = psk_key_hex[2*i];
                char c2 = psk_key_hex[2*i+1];
                int v1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : (c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10) : (c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : -1;
                int v2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : (c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10) : (c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : -1;
                if (v1 < 0 || v2 < 0) { len = 0; break; }
                key_buf[i] = (uint8_t) ((v1 << 4) | v2);
                len++;
            }
        }
        if (len > 0) {
            sec.security_mode = ANJAY_SECURITY_PSK;
            sec.public_cert_or_psk_identity = (const uint8_t *) psk_id;
            sec.public_cert_or_psk_identity_size = strlen(psk_id);
            sec.private_cert_or_psk_key = key_buf;
            sec.private_cert_or_psk_key_size = len;
        }
    }
    anjay_iid_t sec_iid = ANJAY_ID_INVALID;
    int result = anjay_security_object_add_instance(anjay, &sec, &sec_iid);
    if (result) {
        ESP_LOGE(TAG, "Failed to add Security instance: %d", result);
        return result;
    }
    return 0;
}

static int setup_server(anjay_t *anjay) {
#if CONFIG_LWM2M_BOOTSTRAP
    return 0;
#else
    anjay_server_object_purge(anjay);
    anjay_server_instance_t srv = {
        .ssid = CONFIG_LWM2M_SERVER_SHORT_ID,
        .lifetime = 300,
        .default_min_period = 5,
        .default_max_period = 10,
        .disable_timeout = -1,
        .binding = "U",
    };
    anjay_iid_t srv_iid = ANJAY_ID_INVALID;
    int result = anjay_server_object_add_instance(anjay, &srv, &srv_iid);
    if (result) {
        ESP_LOGE(TAG, "Failed to add Server instance: %d", result);
        return result;
    }
    return 0;
#endif
}

static void lwm2m_client_task(void *arg) {
    avs_log_set_default_level(AVS_LOG_DEBUG);
    const anjay_dm_object_def_t *const *dev_obj = NULL;
    const anjay_dm_object_def_t *const *loc_obj = NULL;
    const anjay_dm_object_def_t *const *sm_obj = NULL;
    if (CONFIG_LWM2M_START_DELAY_MS > 0) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LWM2M_START_DELAY_MS));
    }
    resolve_endpoint_name();
    ESP_LOGI(TAG, "Endpoint: %s", g_endpoint_name);

    anjay_configuration_t cfg = {
        .endpoint_name = g_endpoint_name,
        .in_buffer_size = CONFIG_LWM2M_IN_BUFFER_SIZE,
        .out_buffer_size = CONFIG_LWM2M_OUT_BUFFER_SIZE,
        .msg_cache_size = CONFIG_LWM2M_MSG_CACHE_SIZE,
    };

    anjay_t *anjay = anjay_new(&cfg);
    if (!anjay) {
        ESP_LOGE(TAG, "Could not create Anjay instance");
        vTaskDelete(NULL);
    }

    if (anjay_security_object_install(anjay)
        || anjay_server_object_install(anjay)) {
        ESP_LOGE(TAG, "Could not install Security/Server objects");
        goto cleanup;
    }
    if (setup_security(anjay) || setup_server(anjay)) {
        goto cleanup;
    }

    dev_obj = device_object_create(g_endpoint_name);
    if (!dev_obj || anjay_register_object(anjay, dev_obj)) {
        ESP_LOGE(TAG, "Could not register Device (3) object");
        goto cleanup;
    }
    loc_obj = location_object_create();
    if (!loc_obj || anjay_register_object(anjay, loc_obj)) {
        ESP_LOGE(TAG, "Could not register Location (6) object");
        goto cleanup;
    }
    sm_obj = smart_meter_object_create();
    if (!sm_obj || anjay_register_object(anjay, sm_obj)) {
        ESP_LOGE(TAG, "Could not register Smart Meter (10243) object");
        goto cleanup;
    }

    if (fw_update_install(anjay)) {
        ESP_LOGE(TAG, "Could not install Firmware Update object");
        goto cleanup;
    }

    const avs_time_duration_t max_wait = avs_time_duration_from_scalar(100, AVS_TIME_MS);
    while (1) {
        (void) anjay_event_loop_run(anjay, max_wait);
        device_object_update(anjay, dev_obj);
        location_object_update(anjay, loc_obj);
        smart_meter_object_update(anjay, sm_obj);
        if (fw_update_requested()) { break; }
    }

cleanup:
    device_object_release(dev_obj);
    location_object_release(loc_obj);
    smart_meter_object_release(sm_obj);
    anjay_delete(anjay);
    if (fw_update_requested()) {
        fw_update_reboot();
    }
    vTaskDelete(NULL);
}

void lwm2m_client_start(void) {
    xTaskCreate(lwm2m_client_task, "lwm2m", CONFIG_LWM2M_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, NULL);
}
