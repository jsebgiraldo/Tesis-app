// Minimal Anjay client with client-initiated bootstrap and Temperature 3303
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
#include <math.h>
#include "esp_mac.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "device_object.h"
#include "firmware_update.h"
#include "humidity_object.h"
#include "onoff_object.h"
#include "connectivity_object.h"
#include "location_object.h"
#include "bac19_object.h"

#include <anjay/anjay.h>
#include <anjay/security.h>
#include <anjay/server.h>
#include <anjay/ipso_objects.h>
#include <avsystem/commons/avs_time.h>
// Enable richer logs from AVSystem/Anjay to diagnose registration issues
#include <avsystem/commons/avs_log.h>
// Attribute storage: enabled in Kconfig to persist server Write-Attributes across reboots
#include <anjay/attr_storage.h>
// AVSystem stream helpers used to persist/restore attribute storage
#include <avsystem/commons/avs_stream_membuf.h>
#include <avsystem/commons/avs_stream_inbuf.h>
// NVS for non-volatile storage of attributes
#include "nvs.h"

// IPSO Temperature value callback for anjay_ipso_basic_sensor_impl_t
static int ipso_read_temperature(anjay_iid_t iid, void *user_ptr, double *out_value) {
    (void) iid;
    (void) user_ptr;
    TickType_t ticks = xTaskGetTickCount();
    double base = 25.0;
    double phase = (double) (ticks % 16384) / 512.0;
    double delta = 2.5 * sin(phase);
    *out_value = base + delta;
    return 0;
}

// NOTE: keep includes minimal; remove unused dependencies

// Fallback defaults if sdkconfig hasn't yet picked up new Kconfig symbols

#ifndef CONFIG_LWM2M_TASK_STACK_SIZE
#define CONFIG_LWM2M_TASK_STACK_SIZE 8192
#endif

#ifndef CONFIG_LWM2M_START_DELAY_MS
#define CONFIG_LWM2M_START_DELAY_MS 1000
#endif

// Send feature optional; rely primarily on Observe/Notify

// Buffer/cache sizes
#ifndef CONFIG_LWM2M_IN_BUFFER_SIZE
#define CONFIG_LWM2M_IN_BUFFER_SIZE 4000
#endif
#ifndef CONFIG_LWM2M_OUT_BUFFER_SIZE
#define CONFIG_LWM2M_OUT_BUFFER_SIZE 4000
#endif
#ifndef CONFIG_LWM2M_MSG_CACHE_SIZE
#define CONFIG_LWM2M_MSG_CACHE_SIZE 4000
#endif


// #define APP_SERVER_SSID 1 // not used when Send is removed

static const char *TAG = "lwm2m_client";

// IPSO Basic Sensor (Temperature) value reader
// Resolved endpoint name used everywhere (Anjay endpoint and default PSK identity)
static char g_endpoint_name[64] = "";

static const char *resolve_endpoint_name(void) {
    // If configured endpoint is non-empty, use it (sanitize spaces)
    const char *cfg = CONFIG_LWM2M_ENDPOINT_NAME;
    if (cfg && cfg[0] != '\0') {
        size_t len = strlen(cfg);
        if (len >= sizeof(g_endpoint_name)) {
            len = sizeof(g_endpoint_name) - 1;
        }
        memcpy(g_endpoint_name, cfg, len);
        g_endpoint_name[len] = '\0';
        for (char *p = g_endpoint_name; *p; ++p) {
            if (*p == ' ') { *p = '-'; }
        }
        return g_endpoint_name;
    }
    // Fallback: derive from WiFi STA MAC
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        (void) snprintf(g_endpoint_name, sizeof(g_endpoint_name),
                        "esp32c6-%02X%02X%02X%02X%02X%02X",
                        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        (void) snprintf(g_endpoint_name, sizeof(g_endpoint_name), "esp32c6-unknown");
    }
    return g_endpoint_name;
}

// Get current gateway IPv4 of WIFI_STA_DEF; returns true and writes dotted string on success
static bool get_gateway_ipv4(char *out, size_t out_size) {
    if (!out || out_size < 8) { return false; }
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        return false;
    }
    esp_netif_ip_info_t ip_info = {0};
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        return false;
    }
    uint32_t v4 = lwip_ntohl(ip_info.gw.addr);
    if (v4 == 0) {
        return false;
    }
    uint8_t a = (uint8_t) ((v4 >> 24) & 0xFF);
    uint8_t b = (uint8_t) ((v4 >> 16) & 0xFF);
    uint8_t c = (uint8_t) ((v4 >> 8) & 0xFF);
    uint8_t d = (uint8_t) (v4 & 0xFF);
    (void) snprintf(out, out_size, "%u.%u.%u.%u", a, b, c, d);
    return true;
}

// Log currently configured DNS servers using esp_netif API
static void log_dns_servers(void) {
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        ESP_LOGW(TAG, "No WIFI_STA_DEF netif; cannot list DNS servers");
        return;
    }
    for (int i = ESP_NETIF_DNS_MAIN; i <= ESP_NETIF_DNS_FALLBACK; ++i) {
        esp_netif_dns_info_t dns = {0};
        if (esp_netif_get_dns_info(netif, (esp_netif_dns_type_t) i, &dns) == ESP_OK) {
            // ESP-IDF 5.4: esp_netif_dns_info_t has field 'ip' of type esp_ip_addr_t
            if (dns.ip.type == ESP_IPADDR_TYPE_V4) {
                uint32_t v4 = lwip_ntohl(dns.ip.u_addr.ip4.addr);
                uint8_t a = (uint8_t) ((v4 >> 24) & 0xFF);
                uint8_t b = (uint8_t) ((v4 >> 16) & 0xFF);
                uint8_t c = (uint8_t) ((v4 >> 8) & 0xFF);
                uint8_t d = (uint8_t) (v4 & 0xFF);
                ESP_LOGI(TAG, "DNS[%d]=%u.%u.%u.%u", i, a, b, c, d);
            } else if (dns.ip.type == ESP_IPADDR_TYPE_V6) {
                ESP_LOGI(TAG, "DNS[%d]=<IPv6 configured>", i);
            } else {
                ESP_LOGI(TAG, "DNS[%d]=(none)", i);
            }
        }
    }
}

// Ensure a usable DNS server: if none configured, set gateway as DNS
static void ensure_dns_gateway(void) {
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        return;
    }
    // Check current MAIN DNS
    bool has_dns = false;
    {
        esp_netif_dns_info_t cur = {0};
        if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &cur) == ESP_OK) {
            if (cur.ip.type == ESP_IPADDR_TYPE_V4 && cur.ip.u_addr.ip4.addr != 0) {
                has_dns = true;
            }
        }
    }
    if (has_dns) {
        return;
    }
    // Use gateway IP as DNS if available
    esp_netif_ip_info_t ip_info = {0};
    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.gw.addr != 0) {
        esp_netif_dns_info_t dns = {0};
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        dns.ip.u_addr.ip4.addr = ip_info.gw.addr; // already network order
        if (esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK) {
            uint32_t v4 = lwip_ntohl(ip_info.gw.addr);
            uint8_t a = (uint8_t) ((v4 >> 24) & 0xFF);
            uint8_t b = (uint8_t) ((v4 >> 16) & 0xFF);
            uint8_t c = (uint8_t) ((v4 >> 8) & 0xFF);
            uint8_t d = (uint8_t) (v4 & 0xFF);
            ESP_LOGW(TAG, "Configured DNS[MAIN] to gateway %u.%u.%u.%u", a, b, c, d);
        }
    }
}


// No manual periodic updates or client-initiated Send; rely on TB Observe/Read


// --- Helpers: PSK parsing and retry backoff ---
static size_t hex_to_bytes(const char *hex, uint8_t *out, size_t out_size) {
    size_t len = strlen(hex);
    if (len % 2 != 0) {
        return 0;
    }
    size_t bytes = len / 2;
    if (bytes > out_size) {
        return 0;
    }
    for (size_t i = 0; i < bytes; ++i) {
        char c1 = hex[2 * i];
        char c2 = hex[2 * i + 1];
        int v1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0')
                 : (c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10)
                 : (c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : -1;
        int v2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0')
                 : (c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10)
                 : (c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : -1;
        if (v1 < 0 || v2 < 0) {
            return 0;
        }
        out[i] = (uint8_t) ((v1 << 4) | v2);
    }
    return bytes;
}
// Deprecated: LWM2M_SERVER_URI removed; we now build from hostname, scheme, and port

// Resolve hostname to IPv4 string; returns true on success
static bool resolve_hostname_ipv4(const char *hostname, char *ip_out, size_t ip_out_size) {
    if (!hostname || !*hostname || !ip_out || ip_out_size < 8) {
        return false;
    }
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET; // IPv4 only
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
            if (s && *s) {
                ok = true;
                break;
            }
        }
    }
    freeaddrinfo(res);
    return ok;
}

// Build final server URI, optionally replacing host with resolved IP from override hostname
static void build_final_server_uri(char *out, size_t out_size) {
    // Determine scheme and port from Kconfig choices
    const bool is_secure =
#if CONFIG_LWM2M_SERVER_SCHEME_COAPS
        true;
#else
        false;
#endif
    int port = (int) CONFIG_LWM2M_SERVER_PORT;

    // Choose configured hostname and attempt DNS resolution; fallback to gateway if needed
    char resolved_ip[48] = {0};
    const char *host = NULL;
    const char *configured_host = NULL;
    // Prefer user-provided hostname when enabled; otherwise use ThingsBoard demo by default
#if CONFIG_LWM2M_OVERRIDE_HOSTNAME_ENABLE && 0
    configured_host = CONFIG_LWM2M_OVERRIDE_HOSTNAME;
#else
    configured_host = "192.168.3.100";
#endif
    ESP_LOGI(TAG, "Hostname config: '%s' (scheme=%s, port=%d)",
             (configured_host && configured_host[0]) ? configured_host : "(empty)",
             is_secure ? "coaps" : "coap", port);
    if (configured_host && configured_host[0]
            && resolve_hostname_ipv4(configured_host, resolved_ip, sizeof(resolved_ip))) {
        host = resolved_ip;
        ESP_LOGI(TAG, "Resolved hostname '%s' -> %s", configured_host, resolved_ip);
    } else {
        // Use configured hostname literally if resolution fails
        host = (configured_host && configured_host[0]) ? configured_host : "127.0.0.1";
        if (!(configured_host && configured_host[0])) {
            ESP_LOGW(TAG, "Hostname is empty; defaulting to 127.0.0.1");
        } else {
            ESP_LOGW(TAG, "DNS resolution failed for '%s'; using literal host", configured_host);
            // If DNS failed but we have a gateway IP (common case: server on gateway), fallback to gateway
            char gw[32] = {0};
            if (get_gateway_ipv4(gw, sizeof(gw))) {
                ESP_LOGW(TAG, "Falling back to gateway IP %s for server host", gw);
                host = gw;
            }
        }
    }

    (void) snprintf(out, out_size, "%s://%s:%d", is_secure ? "coaps" : "coap", host, port);
    ESP_LOGI(TAG, "Final LwM2M Server URI: %s", out);
}

static int setup_security(anjay_t *anjay) {
    // purge any existing Security(0) instances before configuring
    anjay_security_object_purge(anjay);

    anjay_security_instance_t sec = {
        .ssid = 1,
        .security_mode = ANJAY_SECURITY_NOSEC,
    };

    // Configure server URI based on bootstrap mode
#if CONFIG_LWM2M_BOOTSTRAP
    sec.bootstrap_server = true;
    #ifdef CONFIG_LWM2M_BOOTSTRAP_URI
    sec.server_uri = CONFIG_LWM2M_BOOTSTRAP_URI;
    #else
    sec.server_uri = NULL;
    #endif
#else
    sec.bootstrap_server = false;
    // Build final server URI from scheme/hostname/port
    static char server_uri_buf[160];
    build_final_server_uri(server_uri_buf, sizeof(server_uri_buf));
    sec.server_uri = server_uri_buf;
#endif

    // Configure PSK if requested and using coaps
    const char *uri = sec.server_uri;
    bool uri_secure = (uri && strncmp(uri, "coaps", 5) == 0);
    const char *psk_id = (CONFIG_LWM2M_SECURITY_PSK_ID[0] != '\0')
                                 ? CONFIG_LWM2M_SECURITY_PSK_ID
                                 : g_endpoint_name; // Default to resolved endpoint name
    const char *psk_key_hex = CONFIG_LWM2M_SECURITY_PSK_KEY;
    if (uri_secure && psk_id && psk_key_hex && psk_id[0] != '\0' && psk_key_hex[0] != '\0') {
        uint8_t key_buf[64];
        size_t key_len = hex_to_bytes(psk_key_hex, key_buf, sizeof(key_buf));
        if (key_len == 0) {
            ESP_LOGE(TAG, "Invalid PSK key hex; falling back to NOSEC");
        } else {
            sec.security_mode = ANJAY_SECURITY_PSK;
            sec.public_cert_or_psk_identity = (const uint8_t *) psk_id;
            sec.public_cert_or_psk_identity_size = strlen(psk_id);
            sec.private_cert_or_psk_key = key_buf;
            sec.private_cert_or_psk_key_size = key_len;
        }
    }

    anjay_iid_t sec_iid = ANJAY_ID_INVALID;
    ESP_LOGI(TAG, "Security URI: %s", sec.server_uri ? sec.server_uri : "(null)");
    int result = anjay_security_object_add_instance(anjay, &sec, &sec_iid);
    if (result) {
        ESP_LOGE(TAG, "Failed to add Security instance: %d", result);
        return result;
    }
    #if CONFIG_LWM2M_BOOTSTRAP
    ESP_LOGI(TAG, "Security(0) instance added (iid=%ld) [bootstrap]", (long) sec_iid);
    #else
    ESP_LOGI(TAG, "Security(0) instance added (iid=%ld)", (long) sec_iid);
    #endif
    return 0;
}

static int setup_server(anjay_t *anjay) {
    // In bootstrap mode, do not pre-provision Server(1)
#if CONFIG_LWM2M_BOOTSTRAP
    ESP_LOGI(TAG, "Bootstrap mode: skipping Server(1) factory setup");
    return 0;
#else
    // purge any existing Server(1) instances before configuring
    anjay_server_object_purge(anjay);
    anjay_server_instance_t srv = {
        .ssid = 1,
        .lifetime = 300,
        .default_min_period = 5,
        .default_max_period = 5,
        .disable_timeout = -1,
        .binding = "U",
    };
    anjay_iid_t srv_iid = ANJAY_ID_INVALID;
    int result = anjay_server_object_add_instance(anjay, &srv, &srv_iid);
    if (result) {
        ESP_LOGE(TAG, "Failed to add Server instance: %d", result);
        return result;
    }
    ESP_LOGI(TAG, "Server(1) instance added (iid=%ld)", (long) srv_iid);
    return 0;
#endif
}

// Handle Wi‑Fi/IP events to toggle Anjay offline/online for faster recovery
static void lwm2m_net_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    anjay_t *anjay = (anjay_t *) arg;
    if (!anjay) {
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected -> entering LwM2M offline");
        (void) anjay_transport_enter_offline(anjay, ANJAY_TRANSPORT_SET_ALL);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got IP -> exiting LwM2M offline and scheduling reconnect");
        // If no DNS provided via DHCP, set gateway as DNS, then dump list
        ensure_dns_gateway();
        log_dns_servers();
        (void) anjay_transport_exit_offline(anjay, ANJAY_TRANSPORT_SET_ALL);
        (void) anjay_transport_schedule_reconnect(anjay, ANJAY_TRANSPORT_SET_ALL);
    }
}

static void lwm2m_client_task(void *arg) {
    // Increase log verbosity for AVSystem/Anjay to aid troubleshooting
    avs_log_set_default_level(AVS_LOG_DEBUG);
    // Ensure objects that might be released in cleanup are initialized
    const anjay_dm_object_def_t **dev_obj = NULL;
    const anjay_dm_object_def_t **loc_obj = NULL;
    const anjay_dm_object_def_t **bac_obj = NULL;
    // Optional delay to let Wi‑Fi/ARP settle before first Register
    if (CONFIG_LWM2M_START_DELAY_MS > 0) {
        ESP_LOGI(TAG, "Startup delay %d ms before LwM2M init", (int) CONFIG_LWM2M_START_DELAY_MS);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LWM2M_START_DELAY_MS));
    }
    // Resolve endpoint name (from config or MAC) and log it once
    resolve_endpoint_name();
    ESP_LOGI(TAG, "LwM2M Endpoint: %s", g_endpoint_name);
    // Also dump DNS servers now in case IP was acquired before our event handler registration
    log_dns_servers();

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

#if CONFIG_ANJAY_WITH_ATTR_STORAGE
    // --- Restore Attribute Storage from NVS (persisted Write-Attributes) ---
    {
        nvs_handle_t nvs;
        esp_err_t err = nvs_open("lwm2m", NVS_READONLY, &nvs);
        if (err == ESP_OK) {
            size_t blob_size = 0;
            err = nvs_get_blob(nvs, "attr", NULL, &blob_size);
            if (err == ESP_OK && blob_size > 0) {
                void *buf = malloc(blob_size);
                if (buf) {
                    err = nvs_get_blob(nvs, "attr", buf, &blob_size);
                    if (err == ESP_OK) {
                        avs_stream_inbuf_t in = AVS_STREAM_INBUF_STATIC_INITIALIZER;
                        avs_stream_inbuf_set_buffer(&in, buf, blob_size);
                        if (avs_is_err(anjay_attr_storage_restore(anjay, (avs_stream_t *) &in))) {
                            ESP_LOGW(TAG, "Attr storage restore failed; starting clean");
                        } else {
                            ESP_LOGI(TAG, "Restored %u bytes of LwM2M attributes from NVS", (unsigned) blob_size);
                        }
                    } else {
                        ESP_LOGW(TAG, "nvs_get_blob(attr) failed: %s", esp_err_to_name(err));
                    }
                    free(buf);
                } else {
                    ESP_LOGE(TAG, "OOM allocating %u bytes for attr restore", (unsigned) blob_size);
                }
            } else if (err == ESP_OK) {
                ESP_LOGI(TAG, "No stored attributes found (size=0)");
            } else if (err == ESP_ERR_NVS_NOT_FOUND) {
                ESP_LOGI(TAG, "No stored attributes in NVS");
            } else {
                ESP_LOGW(TAG, "nvs_get_blob(attr) size failed: %s", esp_err_to_name(err));
            }
            nvs_close(nvs);
        } else if (err == ESP_ERR_NVS_NOT_INITIALIZED) {
            ESP_LOGW(TAG, "NVS not initialized; skipping attr restore");
        } else {
            ESP_LOGW(TAG, "nvs_open() failed: %s", esp_err_to_name(err));
        }
    }
#endif // CONFIG_ANJAY_WITH_ATTR_STORAGE

    if (anjay_security_object_install(anjay)
        || anjay_server_object_install(anjay)) {
        ESP_LOGE(TAG, "Could not install Security/Server objects");
        goto cleanup;
    }

    if (setup_security(anjay) || setup_server(anjay)) {
        goto cleanup;
    }

    // Install IPSO Basic Sensor for Temperature (3303) to honor TB observe attrs
    if (anjay_ipso_basic_sensor_install(anjay, 3303, 1)) {
        ESP_LOGE(TAG, "Could not install IPSO Basic Sensor (3303)");
        goto cleanup;
    }
    const anjay_ipso_basic_sensor_impl_t temp_impl = {
        .get_value = ipso_read_temperature,
        .unit = "Cel",
        .min_range_value = -40.0,
        .max_range_value = 125.0
    };
    if (anjay_ipso_basic_sensor_instance_add(anjay, 3303, 0, temp_impl)) {
        ESP_LOGE(TAG, "Could not add IPSO Temperature instance");
        goto cleanup;
    }
    if (anjay_register_object(anjay, humidity_object_def())) {
        ESP_LOGE(TAG, "Could not register 3304 object");
        goto cleanup;
    }
    if (anjay_register_object(anjay, onoff_object_def())) {
        ESP_LOGE(TAG, "Could not register 3312 object");
        goto cleanup;
    }
    if (anjay_register_object(anjay, connectivity_object_def())) {
        ESP_LOGE(TAG, "Could not register Connectivity (4) object");
        goto cleanup;
    }

    // Register Device (3) object to expose attributes (Manufacturer, Model, Serial, FW)
    dev_obj = device_object_create(g_endpoint_name);
    if (!dev_obj || anjay_register_object(anjay, dev_obj)) {
        ESP_LOGE(TAG, "Could not register Device (3) object");
        goto cleanup;
    }

    // Register Location (6) object with default coordinates (optional)
    loc_obj = location_object_create();
    if (!loc_obj || anjay_register_object(anjay, loc_obj)) {
        ESP_LOGE(TAG, "Could not register Location (6) object");
        goto cleanup;
    }

    // Register BAC (19) object for TB OTA metadata compatibility (optional)
    bac_obj = bac19_object_create();
    if (!bac_obj || anjay_register_object(anjay, bac_obj)) {
        ESP_LOGE(TAG, "Could not register BAC (19) object");
        goto cleanup;
    }

    #if CONFIG_LWM2M_BOOTSTRAP
    ESP_LOGI(TAG, "Starting Anjay event loop (bootstrap mode)");
    #else
    ESP_LOGI(TAG, "Starting Anjay event loop");
    #endif
    // Register network event handlers to manage offline/online
    esp_event_handler_instance_t inst_wifi = NULL;
    esp_event_handler_instance_t inst_ip = NULL;
    (void) esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &lwm2m_net_event_handler, anjay, &inst_wifi);
    (void) esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &lwm2m_net_event_handler, anjay, &inst_ip);

    ESP_LOGI(TAG, "Entering LwM2M main loop (server-driven updates).");

    // Install Firmware Update object
    if (fw_update_install(anjay)) {
        ESP_LOGE(TAG, "Could not install Firmware Update object");
        goto cleanup;
    }

    const avs_time_duration_t max_wait = avs_time_duration_from_scalar(100, AVS_TIME_MS);
    uint32_t attr_persist_ticks = 0; // ~periodic persistence timer
    while (1) {
        (void) anjay_event_loop_run(anjay, max_wait);
        // Process Device(3) executes (e.g., Reboot) under server control
        device_object_update(anjay, dev_obj);
        // Update IPSO Temperature (3303); Anjay will honor server-set attributes
        anjay_ipso_basic_sensor_update(anjay, 3303, 0);
        humidity_object_update(anjay);
        onoff_object_update(anjay);
        connectivity_object_update(anjay);
        location_object_update(anjay, loc_obj);
#if CONFIG_ANJAY_WITH_ATTR_STORAGE
        // Periodically persist attributes if modified (about every 5 seconds)
        if (++attr_persist_ticks >= 50) { // 50 * 100ms ~ 5s
            attr_persist_ticks = 0;
            if (anjay_attr_storage_is_modified(anjay)) {
                nvs_handle_t nvs;
                esp_err_t err = nvs_open("lwm2m", NVS_READWRITE, &nvs);
                if (err == ESP_OK) {
                    avs_stream_t *membuf = avs_stream_membuf_create();
                    if (membuf) {
                        if (avs_is_ok(anjay_attr_storage_persist(anjay, membuf))) {
                            // Get buffer ownership to write to NVS
                            void *data_ptr = NULL;
                            size_t data_size = 0;
                            (void) avs_stream_membuf_fit(membuf);
                            if (avs_is_ok(avs_stream_membuf_take_ownership(membuf, &data_ptr, &data_size))) {
                                if (data_ptr && data_size > 0) {
                                    err = nvs_set_blob(nvs, "attr", data_ptr, data_size);
                                    if (err == ESP_OK) {
                                        err = nvs_commit(nvs);
                                    }
                                    if (err == ESP_OK) {
                                        ESP_LOGD(TAG, "Persisted %u bytes of attributes to NVS", (unsigned) data_size);
                                    } else {
                                        ESP_LOGW(TAG, "Failed to persist attr to NVS: %s", esp_err_to_name(err));
                                    }
                                } else {
                                    // No attributes -> erase key
                                    (void) nvs_erase_key(nvs, "attr");
                                    (void) nvs_commit(nvs);
                                    ESP_LOGD(TAG, "Attributes empty; NVS key erased");
                                }
                                if (data_ptr) {
                                    free(data_ptr);
                                }
                            }
                        }
                        (void) avs_stream_cleanup(&membuf);
                    }
                    nvs_close(nvs);
                }
            }
        }
#endif // CONFIG_ANJAY_WITH_ATTR_STORAGE
        // If a firmware update requested reboot, break to cleanup and reboot
        if (fw_update_requested()) {
            break;
        }
    }

cleanup:
    // release dev obj if created (safe with NULL)
#if CONFIG_ANJAY_WITH_ATTR_STORAGE
    // Final persistence on exit if modified
    if (anjay && anjay_attr_storage_is_modified(anjay)) {
        nvs_handle_t nvs;
        if (nvs_open("lwm2m", NVS_READWRITE, &nvs) == ESP_OK) {
            avs_stream_t *membuf = avs_stream_membuf_create();
            if (membuf) {
                if (avs_is_ok(anjay_attr_storage_persist(anjay, membuf))) {
                    void *data_ptr = NULL;
                    size_t data_size = 0;
                    (void) avs_stream_membuf_fit(membuf);
                    if (avs_is_ok(avs_stream_membuf_take_ownership(membuf, &data_ptr, &data_size))) {
                        if (data_ptr && data_size > 0) {
                            if (nvs_set_blob(nvs, "attr", data_ptr, data_size) == ESP_OK) {
                                (void) nvs_commit(nvs);
                            }
                        } else {
                            (void) nvs_erase_key(nvs, "attr");
                            (void) nvs_commit(nvs);
                        }
                        if (data_ptr) {
                            free(data_ptr);
                        }
                    }
                }
                (void) avs_stream_cleanup(&membuf);
            }
            nvs_close(nvs);
        }
    }
#endif // CONFIG_ANJAY_WITH_ATTR_STORAGE
    device_object_release(dev_obj);
    location_object_release(loc_obj);
    bac19_object_release(bac_obj);
    anjay_delete(anjay);
    if (fw_update_requested()) {
        fw_update_reboot();
    }
    vTaskDelete(NULL);
}

void lwm2m_client_start(void) {
    xTaskCreate(lwm2m_client_task, "lwm2m", CONFIG_LWM2M_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, NULL);
}





