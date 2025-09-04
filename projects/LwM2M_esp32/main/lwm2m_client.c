// Minimal Anjay client with client-initiated bootstrap and Temperature 3303
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <math.h>

#include <anjay/anjay.h>
#include <anjay/security.h>
#include <anjay/server.h>
#include <anjay/lwm2m_send.h>
#include <avsystem/commons/avs_time.h>

// TEMP: build marker was here

// Fallback defaults if sdkconfig hasn't yet picked up new Kconfig symbols

#ifndef CONFIG_LWM2M_TASK_STACK_SIZE
#define CONFIG_LWM2M_TASK_STACK_SIZE 8192
#endif

// Retry policy fallbacks
#ifndef CONFIG_LWM2M_RETRY_DELAY_S
#define CONFIG_LWM2M_RETRY_DELAY_S 5
#endif

#ifndef CONFIG_LWM2M_MAX_RETRIES
#define CONFIG_LWM2M_MAX_RETRIES 3
#endif


#define APP_SERVER_SSID 1

static const char *TAG = "lwm2m_client";

static float read_temperature(void) {
    float base = 25.0f;
    float delta = 2.0f * sinf((float)(xTaskGetTickCount() % 10000) / 1000.0f);
    return base + delta;
}

typedef struct {
    const anjay_dm_object_def_t *const *def;
} temp_obj_t;

static int temp_list_instances(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                               anjay_dm_list_ctx_t *ctx) {
    (void) anjay; (void) def;
    anjay_dm_emit(ctx, 0);
    return 0;
}

static int temp_list_resources(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                               anjay_iid_t iid, anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay; (void) def; (void) iid;
    anjay_dm_emit_res(ctx, 5700, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, 5701, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    return 0;
}

static int temp_read(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                     anjay_iid_t iid, anjay_rid_t rid, anjay_riid_t riid,
                     anjay_output_ctx_t *out_ctx) {
    (void) anjay; (void) def; (void) iid; (void) riid;
    switch (rid) {
    case 5700: {
        float t = read_temperature();
        return anjay_ret_float(out_ctx, t);
    }
    case 5701:
        return anjay_ret_string(out_ctx, "Cel");
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static const anjay_dm_object_def_t OBJ3303 = {
    .oid = 3303,
    .handlers = {
        .list_instances = temp_list_instances,
        .list_resources = temp_list_resources,
        .resource_read = temp_read,
    }
};

static const anjay_dm_object_def_t *const OBJ3303_DEF = &OBJ3303;

#if CONFIG_ANJAY_WITH_SEND
static void send_finished_cb(anjay_t *anjay, anjay_ssid_t ssid,
                             const anjay_send_batch_t *batch, int result,
                             void *data) {
    (void) anjay; (void) batch; (void) data;
    if (result == ANJAY_SEND_SUCCESS) {
        ESP_LOGI(TAG, "Send delivered to SSID %d", (int) ssid);
    } else {
        ESP_LOGW(TAG, "Send result SSID %d: %d", (int) ssid, result);
    }
}
#endif

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
        .default_max_period = 60,
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

static void lwm2m_client_task(void *arg) {
    // Optional delay to let Wi‑Fi/ARP settle before first Register
    if (CONFIG_LWM2M_START_DELAY_MS > 0) {
        ESP_LOGI(TAG, "Startup delay %d ms before LwM2M init", (int) CONFIG_LWM2M_START_DELAY_MS);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LWM2M_START_DELAY_MS));
    }
    anjay_configuration_t cfg = {
        .endpoint_name = CONFIG_LWM2M_ENDPOINT_NAME,
        .in_buffer_size = 4000,
        .out_buffer_size = 4000,
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

    temp_obj_t temp = { .def = &OBJ3303_DEF };
    if (anjay_register_object(anjay, temp.def)) {
        ESP_LOGE(TAG, "Could not register 3303 object");
        goto cleanup;
    }

    #if CONFIG_LWM2M_BOOTSTRAP
    ESP_LOGI(TAG, "Starting Anjay event loop (bootstrap mode)");
    #else
    ESP_LOGI(TAG, "Starting Anjay event loop");
    #endif

    // Controlled registration with user-defined timeout and retry policy
    int attempt = 0;
    const avs_time_duration_t max_wait = avs_time_duration_from_scalar(250, AVS_TIME_MS);
    while (1) {
        // Wait for ongoing registration to finish or time out
        const int timeout_s = 5;
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
            goto cleanup;
        }
    ESP_LOGW(TAG, "Scheduling retry %d after %ds", attempt, (int) CONFIG_LWM2M_RETRY_DELAY_S);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LWM2M_RETRY_DELAY_S * 1000));
    // Trigger reconnect for all transports; this will schedule re-register if needed
    (void) anjay_transport_schedule_reconnect(anjay, ANJAY_TRANSPORT_SET_ALL);
    }

    ESP_LOGI(TAG, "LwM2M registered. Entering main loop.");
#if CONFIG_LWM2M_SEND_ENABLE
    ESP_LOGI(TAG, "LwM2M Send enabled, period=%ds", (int) CONFIG_LWM2M_SEND_PERIOD_S);
#endif
    uint32_t last_send_tick = xTaskGetTickCount();
    const uint32_t send_period_ticks = pdMS_TO_TICKS(5 * 1000);
    #if 1
    uint32_t last_notify_tick = xTaskGetTickCount();
    const uint32_t notify_period_ticks = pdMS_TO_TICKS(5 * 1000);
    #endif
    while (1) {
        (void) anjay_event_loop_run(anjay, max_wait);

#if 1 && CONFIG_ANJAY_WITH_SEND && !CONFIG_LWM2M_BOOTSTRAP
        // Periodically push current temperature via LwM2M Send (TS 1.1)
        uint32_t now = xTaskGetTickCount();
        if ((now - last_send_tick) >= send_period_ticks) {
            last_send_tick = now;
            anjay_send_batch_builder_t *builder = anjay_send_batch_builder_new();
            if (builder) {
                // Add current 3303/0/5700 and 3303/0/5701 values
                anjay_send_batch_data_add_current(builder, anjay, 3303, 0, 5700);
                anjay_send_batch_data_add_current(builder, anjay, 3303, 0, 5701);
                anjay_send_batch_t *batch = anjay_send_batch_builder_compile(&builder);
                if (batch) {
                    anjay_send_result_t r = anjay_send_deferrable(anjay, APP_SERVER_SSID, batch, send_finished_cb, NULL);
                    if (r != ANJAY_SEND_OK) {
                        ESP_LOGW(TAG, "Send deferred/failed: %d", (int) r);
                    } else {
                        ESP_LOGI(TAG, "Send queued -> SSID %d", APP_SERVER_SSID);
                    }
                    anjay_send_batch_release(&batch);
                } else {
                    ESP_LOGW(TAG, "Failed to compile send batch");
                }
            }
        }
#endif

#if 1
        uint32_t now2 = xTaskGetTickCount();
        if ((now2 - last_notify_tick) >= notify_period_ticks) {
            last_notify_tick = now2;
#ifdef ANJAY_WITH_OBSERVATION_STATUS
            anjay_resource_observation_status_t st =
                anjay_resource_observation_status(anjay, 3303, 0, 5700);
            ESP_LOGI(TAG, "OBS status 3303/0/5700: observed=%d pmin=%ld",
                     (int) st.is_observed, (long) st.min_period);
#endif
            (void) anjay_notify_changed(anjay, 3303, 0, 5700);
            ESP_LOGI(TAG, "Notify queued for 3303/0/5700");
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
