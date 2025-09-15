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
#include "esp_mac.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "temp_object.h"

#include <anjay/anjay.h>
#include <anjay/security.h>
#include <anjay/server.h>
#include <anjay/lwm2m_send.h>
#include <anjay/attr_storage.h>
#include <avsystem/commons/avs_time.h>

// NOTE: keep includes minimal; remove unused dependencies

// Fallback defaults if sdkconfig hasn't yet picked up new Kconfig symbols

#ifndef CONFIG_LWM2M_TASK_STACK_SIZE
#define CONFIG_LWM2M_TASK_STACK_SIZE 8192
#endif

#ifndef CONFIG_LWM2M_START_DELAY_MS
#define CONFIG_LWM2M_START_DELAY_MS 1000
#endif

#ifndef CONFIG_LWM2M_SEND_ENABLE
#define CONFIG_LWM2M_SEND_ENABLE 1
#endif

#ifndef CONFIG_LWM2M_SEND_PERIOD_S
#define CONFIG_LWM2M_SEND_PERIOD_S 30
#endif

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


#define APP_SERVER_SSID 1

static const char *TAG = "lwm2m_client";

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


#if CONFIG_LWM2M_SEND_ENABLE
static void send_finished_cb(anjay_t *anjay, anjay_ssid_t ssid,
                             const anjay_send_batch_t *batch, int result,
                             void *data) {
    (void) anjay; (void) batch; (void) data;
    switch (result) {
    case ANJAY_SEND_SUCCESS:
        ESP_LOGI(TAG, "Send delivered to SSID %d", (int) ssid);
        break;
#ifdef ANJAY_WITH_SEND
    case ANJAY_SEND_TIMEOUT:
        ESP_LOGW(TAG, "Send timeout on SSID %d", (int) ssid);
        break;
    case ANJAY_SEND_ABORT:
        ESP_LOGW(TAG, "Send aborted on SSID %d (offline/cleanup)", (int) ssid);
        break;
    case ANJAY_SEND_DEFERRED_ERROR:
        ESP_LOGE(TAG, "Send deferred error on SSID %d (offline or protocol doesn’t support Send)", (int) ssid);
        break;
#endif
    default:
        ESP_LOGW(TAG, "Send result SSID %d: %d", (int) ssid, result);
        break;
    }
}
#endif

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

    // Resolve hostname if enabled, else use literal hostname string
    char host_ip[48] = {0};
    const char *host = NULL;
#if CONFIG_LWM2M_OVERRIDE_HOSTNAME_ENABLE
    const char *override_host = CONFIG_LWM2M_OVERRIDE_HOSTNAME;
    ESP_LOGI(TAG, "Hostname config: '%s' (scheme=%s, port=%d)",
             (override_host && override_host[0]) ? override_host : "(empty)",
             is_secure ? "coaps" : "coap", port);
    if (override_host && override_host[0] && resolve_hostname_ipv4(override_host, host_ip, sizeof(host_ip))) {
        host = host_ip;
        ESP_LOGI(TAG, "Resolved hostname '%s' -> %s", override_host, host_ip);
    } else {
        // Use configured hostname literally if resolution fails
        host = override_host && override_host[0] ? override_host : "127.0.0.1";
        if (!(override_host && override_host[0])) {
            ESP_LOGW(TAG, "Hostname override enabled but empty; defaulting to 127.0.0.1");
        } else {
            ESP_LOGW(TAG, "DNS resolution failed for '%s'; using literal host", override_host);
            // If DNS failed but we have a gateway IP (common case: server on gateway), fallback to gateway
            char gw[32] = {0};
            if (get_gateway_ipv4(gw, sizeof(gw))) {
                ESP_LOGW(TAG, "Falling back to gateway IP %s for server host", gw);
                host = gw;
            }
        }
    }
#else
    host = "127.0.0.1";
#endif

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
        .default_max_period = 300,
        .disable_timeout = 86400,
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

    if (anjay_security_object_install(anjay)
        || anjay_server_object_install(anjay)) {
        ESP_LOGE(TAG, "Could not install Security/Server objects");
        goto cleanup;
    }

    if (setup_security(anjay) || setup_server(anjay)) {
        goto cleanup;
    }

    if (anjay_register_object(anjay, temp_object_def())) {
        ESP_LOGE(TAG, "Could not register 3303 object");
        goto cleanup;
    }

#ifdef ANJAY_WITH_ATTR_STORAGE
    // Set pmin/pmax only for Temperature object (OID 3303), instance 0
    {
        const anjay_oid_t OID_TEMP = 3303;
        const anjay_iid_t IID0 = 0;
        anjay_dm_oi_attributes_t attrs = ANJAY_DM_OI_ATTRIBUTES_EMPTY;
    attrs.min_period = 10;  // notify no more often than every 10s
    attrs.max_period = 10;  // force at least one notify every 10s
    int r = anjay_attr_storage_set_instance_attrs(anjay, APP_SERVER_SSID, OID_TEMP, IID0, &attrs);
        if (r) {
            ESP_LOGW(TAG, "Failed to set attrs for 3303/0 (r=%d)", r);
        } else {
            ESP_LOGI(TAG, "Set pmin=%d, pmax=%d for 3303/0", (int) attrs.min_period, (int) attrs.max_period);
        }
    }
#endif // ANJAY_WITH_ATTR_STORAGE

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

    ESP_LOGI(TAG, "Entering LwM2M main loop.");

    const avs_time_duration_t max_wait = avs_time_duration_from_scalar(250, AVS_TIME_MS);

    // Periodically flag temperature as changed (Notify) and Send data
    const int NOTIFY_PERIOD_S = 10; // match attrs.min_period/max_period above
    const int SEND_PERIOD_S = 5;    // independent Send to compare with Notify
    TickType_t last_notify_tick = xTaskGetTickCount();
    TickType_t last_send_tick = last_notify_tick;

    while (1) {
        (void) anjay_event_loop_run(anjay, max_wait);

        TickType_t now = xTaskGetTickCount();
        // Notify path: mark resource changed every NOTIFY_PERIOD_S
        if ((now - last_notify_tick) >= pdMS_TO_TICKS(NOTIFY_PERIOD_S * 1000)) {
            last_notify_tick = now;
            // Mark 3303/0/5700 as changed; Anjay enforces pmin/pmax and sends only if observed
            (void) anjay_notify_changed(anjay, 3303, 0, 5700);
            ESP_LOGI(TAG, "Temperature changed (Notify)");
        }

        // Send path: push current values every SEND_PERIOD_S
#if CONFIG_LWM2M_SEND_ENABLE
        if ((now - last_send_tick) >= pdMS_TO_TICKS(SEND_PERIOD_S * 1000)) {
            last_send_tick = now;
            anjay_send_batch_builder_t *b = anjay_send_batch_builder_new();
            if (b) {
                (void) anjay_send_batch_data_add_current(b, anjay, 3303, 0, 5700);
                (void) anjay_send_batch_data_add_current(b, anjay, 3303, 0, 5701);
                anjay_send_batch_t *batch = anjay_send_batch_builder_compile(&b);
                if (batch) {
                    ESP_LOGI(TAG, "Queueing LwM2M Send for 3303/0/5700,5701");
                    int sret = anjay_send_deferrable(anjay, APP_SERVER_SSID, batch, send_finished_cb, NULL);
                    if (sret != ANJAY_SEND_OK) {
                        ESP_LOGW(TAG, "Send queue returned %d (muted/offline/bootstrap/protocol?)", sret);
                    }
                    anjay_send_batch_release(&batch);
                } else {
                    anjay_send_batch_builder_cleanup(&b);
                }
                ESP_LOGI(TAG, "Temperature sent (Send)");
            }
        }
#endif

    }
cleanup:
    anjay_delete(anjay);
    vTaskDelete(NULL);
}

void lwm2m_client_start(void) {
    xTaskCreate(lwm2m_client_task, "lwm2m", CONFIG_LWM2M_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, NULL);
}
