// Minimal Anjay client with client-initiated bootstrap and Temperature 3303
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include <string.h>
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

#ifndef CONFIG_LWM2M_CONNECT_TIMEOUT_S
#define CONFIG_LWM2M_CONNECT_TIMEOUT_S 30
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


// Retry policy fallbacks
#ifndef CONFIG_LWM2M_RETRY_DELAY_S
#define CONFIG_LWM2M_RETRY_DELAY_S 5
#endif

#ifndef CONFIG_LWM2M_MAX_RETRIES
#define CONFIG_LWM2M_MAX_RETRIES 3
#endif

// Advanced retry controls (cap and jitter)
#ifndef CONFIG_LWM2M_RETRY_MAX_DELAY_S
#define CONFIG_LWM2M_RETRY_MAX_DELAY_S 60
#endif
#ifndef CONFIG_LWM2M_RETRY_JITTER_PCT
#define CONFIG_LWM2M_RETRY_JITTER_PCT 20
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

static int compute_retry_delay_s(int attempt) {
    // attempt is 1-based
    if (attempt <= 0) {
        return CONFIG_LWM2M_RETRY_DELAY_S;
    }
    // Exponential backoff with cap
    int64_t delay = (int64_t) CONFIG_LWM2M_RETRY_DELAY_S;
    for (int i = 1; i < attempt; ++i) {
        delay <<= 1;
        if (delay >= CONFIG_LWM2M_RETRY_MAX_DELAY_S) {
            delay = CONFIG_LWM2M_RETRY_MAX_DELAY_S;
            break;
        }
    }
    if (delay > CONFIG_LWM2M_RETRY_MAX_DELAY_S) {
        delay = CONFIG_LWM2M_RETRY_MAX_DELAY_S;
    }
    // Apply jitter +/- CONFIG_LWM2M_RETRY_JITTER_PCT%
    if (CONFIG_LWM2M_RETRY_JITTER_PCT > 0) {
        uint32_t r = (uint32_t) xTaskGetTickCount();
        // xorshift mix for better distribution
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        int jitter_span = (int) (delay * CONFIG_LWM2M_RETRY_JITTER_PCT / 100);
        int jitter = (int) (r % (uint32_t) (jitter_span * 2 + 1)) - jitter_span;
        int jittered = (int) delay + jitter;
        if (jittered < 1) {
            jittered = 1;
        }
        return jittered;
    }
    return (int) delay;
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
    sec.server_uri = CONFIG_LWM2M_SERVER_URI;
#endif

    // Configure PSK if requested and using coaps
    const char *uri = sec.server_uri;
    bool uri_secure = (uri && strncmp(uri, "coaps", 5) == 0);
    const char *psk_id = CONFIG_LWM2M_SECURITY_PSK_ID;
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
    anjay_configuration_t cfg = {
        .endpoint_name = CONFIG_LWM2M_ENDPOINT_NAME,
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

    // Controlled registration with user-defined timeout and retry policy
    int attempt = 0;
    const avs_time_duration_t max_wait = avs_time_duration_from_scalar(250, AVS_TIME_MS);

    // Register network event handlers to manage offline/online
    esp_event_handler_instance_t inst_wifi = NULL;
    esp_event_handler_instance_t inst_ip = NULL;
    (void) esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &lwm2m_net_event_handler, anjay, &inst_wifi);
    (void) esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &lwm2m_net_event_handler, anjay, &inst_ip);
    while (1) {
        // Wait for ongoing registration to finish or time out
        const int timeout_s = CONFIG_LWM2M_CONNECT_TIMEOUT_S;
        avs_time_real_t start = avs_time_real_now();
        while (anjay_ongoing_registration_exists(anjay)) {
            (void) anjay_event_loop_run(anjay, max_wait);
            avs_time_duration_t elapsed = avs_time_real_diff(avs_time_real_now(), start);
            if (elapsed.seconds >= timeout_s) {
                ESP_LOGW(TAG, "Register attempt %d timed out after %d s", attempt + 1, timeout_s);
                break;
            }
        }

        if (!anjay_all_connections_failed(anjay) && !anjay_ongoing_registration_exists(anjay)) {
            // Registered successfully
            break;
        }

        // Failed: decide on another attempt
        attempt++;
        if (CONFIG_LWM2M_MAX_RETRIES > 0 && attempt >= CONFIG_LWM2M_MAX_RETRIES) {
            ESP_LOGE(TAG, "Registration failed after %d attempts. Stopping client.", attempt);
            // Unregister handlers before exit
            if (inst_wifi) {
                (void) esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, inst_wifi);
            }
            if (inst_ip) {
                (void) esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, inst_ip);
            }
            goto cleanup;
        }
        int delay_s = compute_retry_delay_s(attempt);
        ESP_LOGW(TAG, "Scheduling retry %d after %ds (base=%ds, cap=%ds, jitter=%d%%)",
                 attempt, delay_s, (int) CONFIG_LWM2M_RETRY_DELAY_S,
                 (int) CONFIG_LWM2M_RETRY_MAX_DELAY_S, (int) CONFIG_LWM2M_RETRY_JITTER_PCT);
        vTaskDelay(pdMS_TO_TICKS(delay_s * 1000));
        // Trigger reconnect for all transports; this will schedule re-register if needed
        (void) anjay_transport_schedule_reconnect(anjay, ANJAY_TRANSPORT_SET_ALL);
    }

    ESP_LOGI(TAG, "LwM2M registered. Entering main loop.");

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
