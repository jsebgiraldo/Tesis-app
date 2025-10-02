// Enhanced Anjay client registering Device(3), Location(6), Connectivity(4), BAC(19) and Smart Meter (10243)
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
#include "connectivity_object.h"
#include "bac19_object.h"

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
// Fallbacks for optional Kconfig values (mirror TEMP project robustness)
#ifndef CONFIG_LWM2M_SERVER_SHORT_ID
#define CONFIG_LWM2M_SERVER_SHORT_ID 123
#endif
#ifndef CONFIG_LWM2M_SECURITY_PSK_ID
#define CONFIG_LWM2M_SECURITY_PSK_ID ""
#endif
#ifndef CONFIG_LWM2M_SECURITY_PSK_KEY
#define CONFIG_LWM2M_SECURITY_PSK_KEY ""
#endif
#ifndef CONFIG_LWM2M_SERVER_PORT
#define CONFIG_LWM2M_SERVER_PORT 5685
#endif
#ifndef CONFIG_LWM2M_OBS_FALLBACK_PERIOD_S
#define CONFIG_LWM2M_OBS_FALLBACK_PERIOD_S 30
#endif

static char g_endpoint_name[64] = "";

static const char *resolve_endpoint_name(void) {
    uint8_t mac[6] = {0};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
        if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK) {
            memset(mac, 0, sizeof(mac));
        }
    }
    snprintf(g_endpoint_name, sizeof(g_endpoint_name), "ESP32C6-SM-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return g_endpoint_name;
}

// Try to obtain the default gateway IPv4 address as a string.
// Returns true on success and writes into ip_out.
static bool get_gateway_ipv4(char *out, size_t out_size) {
    if (!out || out_size < 8) { return false; }
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) { return false; }
    esp_netif_ip_info_t ip_info = {0};
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) { return false; }
    uint32_t v4 = lwip_ntohl(ip_info.gw.addr);
    if (v4 == 0) return false;
    uint8_t a = (uint8_t)((v4>>24)&0xFF), b=(uint8_t)((v4>>16)&0xFF), c=(uint8_t)((v4>>8)&0xFF), d=(uint8_t)(v4&0xFF);
    snprintf(out, out_size, "%u.%u.%u.%u", a,b,c,d);
    return true;
}

static bool resolve_hostname_ipv4(const char *hostname, char *ip_out, size_t ip_out_size) {
    if (!hostname || !*hostname || !ip_out || ip_out_size < 8) return false;
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo *res = NULL; int err = getaddrinfo(hostname, NULL, &hints, &res);
    if (err != 0 || !res) { ESP_LOGW(TAG, "getaddrinfo('%s') failed: %d", hostname, err); return false; }
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

static bool is_ipv4_literal(const char *s) {
    if (!s || !*s) {
        return false;
    }
    ip4_addr_t addr;
    return inet_aton(s, &addr) == 1;
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
    const char *configured_host = NULL;
#if CONFIG_LWM2M_OVERRIDE_HOSTNAME_ENABLE
    configured_host = CONFIG_LWM2M_OVERRIDE_HOSTNAME;
#else
    configured_host = "192.168.3.100"; // fallback demo
#endif
    const char *host = NULL;
    if (configured_host && configured_host[0]) {
        if (is_ipv4_literal(configured_host)) {
            host = configured_host;
        } else if (resolve_hostname_ipv4(configured_host, resolved_ip, sizeof(resolved_ip))) {
            host = resolved_ip;
        } else {
            char gw[32] = {0};
            if (get_gateway_ipv4(gw, sizeof(gw))) {
                ESP_LOGW(TAG, "Hostname '%s' unresolved; using gateway %s", configured_host, gw);
                host = gw;
            } else {
                host = configured_host; // last resort literal
            }
        }
    } else {
        host = "127.0.0.1";
    }
    snprintf(out, out_size, "%s://%s:%d", is_secure ? "coaps" : "coap", host, port);
    ESP_LOGI(TAG, "Final LwM2M Server URI: %s", out);
}

static size_t hex_to_bytes(const char *hex, uint8_t *out, size_t out_size) {
    size_t len = strlen(hex); if (len % 2 != 0) return 0; size_t bytes = len/2; if (bytes>out_size) return 0;
    for (size_t i=0;i<bytes;i++){ char c1=hex[2*i], c2=hex[2*i+1]; int v1=(c1>='0'&&c1<='9')?(c1-'0'):(c1>='a'&&c1<='f')?(c1-'a'+10):(c1>='A'&&c1<='F')?(c1-'A'+10):-1; int v2=(c2>='0'&&c2<='9')?(c2-'0'):(c2>='a'&&c2<='f')?(c2-'a'+10):(c2>='A'&&c2<='F')?(c2-'A'+10):-1; if(v1<0||v2<0) return 0; out[i]=(uint8_t)((v1<<4)|v2);} return bytes; }

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
    if (uri_secure && psk_id && psk_key_hex && psk_id[0] && psk_key_hex[0]) {
        uint8_t key_buf[64]; size_t key_len = hex_to_bytes(psk_key_hex, key_buf, sizeof(key_buf));
        if (key_len > 0) {
            sec.security_mode = ANJAY_SECURITY_PSK;
            sec.public_cert_or_psk_identity = (const uint8_t *) psk_id;
            sec.public_cert_or_psk_identity_size = strlen(psk_id);
            sec.private_cert_or_psk_key = key_buf;
            sec.private_cert_or_psk_key_size = key_len;
        } else {
            ESP_LOGE(TAG, "Invalid PSK hex, falling back to NOSEC");
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

static void lwm2m_net_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    anjay_t *anjay = (anjay_t *) arg; if (!anjay) return;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected -> LwM2M offline");
        anjay_transport_enter_offline(anjay, ANJAY_TRANSPORT_SET_ALL);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got IP -> LwM2M reconnect");
        anjay_transport_exit_offline(anjay, ANJAY_TRANSPORT_SET_ALL);
        anjay_transport_schedule_reconnect(anjay, ANJAY_TRANSPORT_SET_ALL);
        anjay_notify_instances_changed(anjay, 4); // connectivity
        anjay_notify_instances_changed(anjay, 10243);
    }
}

static void lwm2m_client_task(void *arg) {
    avs_log_set_default_level(AVS_LOG_DEBUG);
    const anjay_dm_object_def_t *const *dev_obj = NULL;
    const anjay_dm_object_def_t *const *loc_obj = NULL;
    const anjay_dm_object_def_t *const *sm_obj = NULL;
    const anjay_dm_object_def_t *const *bac_obj = NULL;
    if (CONFIG_LWM2M_START_DELAY_MS > 0) {
        ESP_LOGI(TAG, "Startup delay %d ms", (int) CONFIG_LWM2M_START_DELAY_MS);
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
#ifdef ANJAY_WITH_LWM2M11
    static const anjay_lwm2m_version_config_t VER11_ONLY = { .minimum_version=ANJAY_LWM2M_VERSION_1_1, .maximum_version=ANJAY_LWM2M_VERSION_1_1 };
    cfg.lwm2m_version_config = &VER11_ONLY;
#endif
    anjay_t *anjay = anjay_new(&cfg);
    if (!anjay) { ESP_LOGE(TAG, "Could not create Anjay instance"); vTaskDelete(NULL); }
    if (anjay_security_object_install(anjay) || anjay_server_object_install(anjay)) { ESP_LOGE(TAG,"Install Security/Server failed"); goto cleanup; }
    if (setup_security(anjay) || setup_server(anjay)) goto cleanup;

    if (anjay_register_object(anjay, connectivity_object_def())) { ESP_LOGE(TAG,"Register Connectivity failed"); goto cleanup; }
    dev_obj = device_object_create(g_endpoint_name); if (!dev_obj || anjay_register_object(anjay, dev_obj)) { ESP_LOGE(TAG,"Register Device failed"); goto cleanup; }
    loc_obj = location_object_create(); if (!loc_obj || anjay_register_object(anjay, loc_obj)) { ESP_LOGE(TAG,"Register Location failed"); goto cleanup; }
    bac_obj = bac19_object_create(); if (!bac_obj || anjay_register_object(anjay, bac_obj)) { ESP_LOGE(TAG,"Register BAC19 failed"); goto cleanup; }
    sm_obj = smart_meter_object_create(); if (!sm_obj || anjay_register_object(anjay, sm_obj)) { ESP_LOGE(TAG,"Register Smart Meter failed"); goto cleanup; }

    if (fw_update_install(anjay)) { ESP_LOGE(TAG, "Firmware Update install failed"); goto cleanup; }

#if CONFIG_ANJAY_WITH_ATTR_STORAGE
    // Restore attr storage from NVS after object registration
    {
        nvs_handle_t nvs; if (nvs_open("lwm2m", NVS_READONLY, &nvs) == ESP_OK) {
            size_t blob_size=0; if (nvs_get_blob(nvs, "attr", NULL, &blob_size)==ESP_OK && blob_size>0) {
                void *buf = malloc(blob_size); if (buf) { if (nvs_get_blob(nvs,"attr",buf,&blob_size)==ESP_OK) {
                        avs_stream_inbuf_t in = AVS_STREAM_INBUF_STATIC_INITIALIZER; avs_stream_inbuf_set_buffer(&in, buf, blob_size);
                        if (avs_is_err(anjay_attr_storage_restore(anjay,(avs_stream_t*)&in))) { ESP_LOGW(TAG,"Attr restore failed"); } else { ESP_LOGI(TAG,"Restored %u bytes of attributes", (unsigned)blob_size); }
                    }
                    free(buf);
                }
            }
            nvs_close(nvs);
        }
    }
#endif
    esp_event_handler_instance_t inst_wifi=NULL, inst_ip=NULL;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &lwm2m_net_event_handler, anjay, &inst_wifi);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &lwm2m_net_event_handler, anjay, &inst_ip);

    ESP_LOGI(TAG, "Entering LwM2M loop");
    anjay_notify_instances_changed(anjay, 4);
    anjay_notify_instances_changed(anjay, 10243);
    const avs_time_duration_t max_wait = avs_time_duration_from_scalar(100, AVS_TIME_MS);
    uint32_t attr_persist_ticks = 0;
    while (1) {
        anjay_event_loop_run(anjay, max_wait);
        connectivity_object_update(anjay);
        device_object_update(anjay, dev_obj);
        location_object_update(anjay, loc_obj);
        smart_meter_object_update(anjay, sm_obj);
#if CONFIG_ANJAY_WITH_ATTR_STORAGE
        if (++attr_persist_ticks >= 50) { // ~5s (@100ms loop)
            attr_persist_ticks = 0;
            if (anjay_attr_storage_is_modified(anjay)) {
                nvs_handle_t nvs; if (nvs_open("lwm2m", NVS_READWRITE, &nvs)==ESP_OK) {
                    avs_stream_t *membuf = avs_stream_membuf_create();
                    if (membuf) {
                        if (avs_is_ok(anjay_attr_storage_persist(anjay, membuf))) {
                            void *data_ptr=NULL; size_t data_size=0; avs_stream_membuf_fit(membuf);
                            if (avs_is_ok(avs_stream_membuf_take_ownership(membuf,&data_ptr,&data_size))) {
                                if (data_ptr && data_size>0) { if (nvs_set_blob(nvs,"attr",data_ptr,data_size)==ESP_OK) nvs_commit(nvs); }
                                else { nvs_erase_key(nvs,"attr"); nvs_commit(nvs); }
                                if (data_ptr) free(data_ptr);
                            }
                        }
                        avs_stream_cleanup(&membuf);
                    }
                    nvs_close(nvs);
                }
            }
        }
#endif
        if (fw_update_requested()) break;
    }

cleanup:
#if CONFIG_ANJAY_WITH_ATTR_STORAGE
    if (fw_update_requested() == false) {
        if (/* intentionally final persist */ 1) {
            nvs_handle_t nvs; if (nvs_open("lwm2m", NVS_READWRITE, &nvs)==ESP_OK) {
                if (anjay_attr_storage_is_modified(anjay)) {
                    avs_stream_t *membuf = avs_stream_membuf_create(); if (membuf) {
                        if (avs_is_ok(anjay_attr_storage_persist(anjay,membuf))) {
                            void *data_ptr=NULL; size_t data_size=0; avs_stream_membuf_fit(membuf);
                            if (avs_is_ok(avs_stream_membuf_take_ownership(membuf,&data_ptr,&data_size))) {
                                if (data_ptr && data_size>0) { if (nvs_set_blob(nvs,"attr",data_ptr,data_size)==ESP_OK) nvs_commit(nvs); } else { nvs_erase_key(nvs,"attr"); nvs_commit(nvs); }
                                if (data_ptr) free(data_ptr);
                            }
                        }
                        avs_stream_cleanup(&membuf);
                    }
                }
                nvs_close(nvs);
            }
        }
    }
#endif
    device_object_release(dev_obj);
    location_object_release(loc_obj);
    bac19_object_release(bac_obj);
    smart_meter_object_release(sm_obj);
    anjay_delete(anjay);
    if (fw_update_requested()) fw_update_reboot();
    vTaskDelete(NULL);
}

void lwm2m_client_start(void) {
    xTaskCreate(lwm2m_client_task, "lwm2m", CONFIG_LWM2M_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, NULL);
}
