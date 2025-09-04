// Minimal Anjay client with client-initiated bootstrap and Temperature 3303
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <math.h>

#include <anjay/anjay.h>
#include <anjay/security.h>
#include <anjay/server.h>
#include <avsystem/commons/avs_time.h>

// TEMP: build marker was here

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

void lwm2m_client_start(void) {
    anjay_configuration_t cfg = {
        .endpoint_name = CONFIG_LWM2M_ENDPOINT_NAME,
        .in_buffer_size = 4000,
        .out_buffer_size = 4000,
    };

    anjay_t *anjay = anjay_new(&cfg);
    if (!anjay) {
        ESP_LOGE(TAG, "Could not create Anjay instance");
        return;
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

    const avs_time_duration_t max_wait = avs_time_duration_from_scalar(1, AVS_TIME_S);
    while (1) {
        (void) anjay_event_loop_run(anjay, max_wait);
    }

cleanup:
    anjay_delete(anjay);
}
