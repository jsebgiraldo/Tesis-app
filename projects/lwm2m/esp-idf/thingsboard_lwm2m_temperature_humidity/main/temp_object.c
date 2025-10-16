#include "temp_object.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#define OID_TEMPERATURE 3303
#define IID_DEFAULT 0
#define RID_SENSOR_VALUE 5700
#define RID_SENSOR_UNITS 5701
#define RID_MIN_MEASURED 5601
#define RID_MAX_MEASURED 5602
#define RID_RESET_MIN_MAX 5605

#define TEMP_SAMPLE_INTERVAL_MS 1000
#define TEMP_DELTA_EPS 0.001f

static const char *TAG = "temp_obj";

static float read_temperature_sensor(void) {
    TickType_t ticks = xTaskGetTickCount();
    float base = 25.0f;
    // Faster phase to ensure noticeable change across a few seconds
    float phase = (float) (ticks % 8192) / 128.0f;
    float delta = 2.5f * sinf(phase);
    // Deterministic small dither (sawtooth ~ +/-0.05C) to guarantee change between close samples
    float saw = ((float) (ticks & 63) / 63.0f - 0.5f) * 0.10f;
    return base + delta + saw;
}

static bool g_have_value = false;
static float g_current_value = 0.0f;
static float g_min_measured = 0.0f;
static float g_max_measured = 0.0f;
static float g_last_notified = 0.0f;
static TickType_t g_last_notify_tick = 0;
static TickType_t g_last_sample_tick = 0;

static bool record_sample(float value, bool *min_changed, bool *max_changed) {
    if (min_changed) { *min_changed = false; }
    if (max_changed) { *max_changed = false; }

    bool first = !g_have_value;
    if (first) {
        g_min_measured = value;
        g_max_measured = value;
        if (min_changed) { *min_changed = true; }
        if (max_changed) { *max_changed = true; }
    } else {
        if (value < g_min_measured) {
            g_min_measured = value;
            if (min_changed) { *min_changed = true; }
        }
        if (value > g_max_measured) {
            g_max_measured = value;
            if (max_changed) { *max_changed = true; }
        }
    }

    g_current_value = value;
    g_have_value = true;
    return first;
}

static void ensure_sample(void) {
    if (!g_have_value) {
        (void) record_sample(read_temperature_sensor(), NULL, NULL);
        g_last_notified = g_current_value;
        g_last_notify_tick = xTaskGetTickCount();
        ESP_LOGD(TAG, "init sample: value=%.3fC min=%.3f max=%.3f", g_current_value, g_min_measured, g_max_measured);
    }
}

static int temp_list_instances(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *def,
                               anjay_dm_list_ctx_t *ctx) {
    (void) anjay; (void) def;
    anjay_dm_emit(ctx, IID_DEFAULT);
    return 0;
}

static int temp_list_resources(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *def,
                               anjay_iid_t iid,
                               anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay; (void) def; (void) iid;
    anjay_dm_emit_res(ctx, RID_MIN_MEASURED, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_MAX_MEASURED, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_RESET_MIN_MAX, ANJAY_DM_RES_E, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_SENSOR_VALUE, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_SENSOR_UNITS, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    return 0;
}

static int temp_read(anjay_t *anjay,
                     const anjay_dm_object_def_t *const *def,
                     anjay_iid_t iid,
                     anjay_rid_t rid,
                     anjay_riid_t riid,
                     anjay_output_ctx_t *ctx) {
    (void) anjay; (void) def; (void) iid; (void) riid;
    ensure_sample();
    switch (rid) {
    case RID_SENSOR_VALUE:
        // Take a fresh sample on read so pmax-driven notifies also carry new data
        {
            bool dummy_min = false, dummy_max = false;
            float value = read_temperature_sensor();
            (void) record_sample(value, &dummy_min, &dummy_max);
            ESP_LOGD(TAG, "READ /3303/0/5700 -> %.3fC (fresh)", g_current_value);
            return anjay_ret_float(ctx, g_current_value);
        }
    case RID_SENSOR_UNITS:
        ESP_LOGD(TAG, "READ /3303/0/5701 -> 'Cel'");
        return anjay_ret_string(ctx, "Cel");
    case RID_MIN_MEASURED:
        ESP_LOGD(TAG, "READ /3303/0/5601 -> %.3fC", g_min_measured);
        return anjay_ret_float(ctx, g_min_measured);
    case RID_MAX_MEASURED:
        ESP_LOGD(TAG, "READ /3303/0/5602 -> %.3fC", g_max_measured);
        return anjay_ret_float(ctx, g_max_measured);
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int temp_execute(anjay_t *anjay,
                        const anjay_dm_object_def_t *const *def,
                        anjay_iid_t iid,
                        anjay_rid_t rid,
                        anjay_execute_ctx_t *arg_ctx) {
    (void) def; (void) iid; (void) arg_ctx;
    switch (rid) {
    case RID_RESET_MIN_MAX: {
        float value = read_temperature_sensor();
        g_have_value = false;
        (void) record_sample(value, NULL, NULL);
        g_last_notified = value;
        g_last_notify_tick = xTaskGetTickCount();
        anjay_notify_changed(anjay, OID_TEMPERATURE, IID_DEFAULT, RID_MIN_MEASURED);
        anjay_notify_changed(anjay, OID_TEMPERATURE, IID_DEFAULT, RID_MAX_MEASURED);
        anjay_notify_changed(anjay, OID_TEMPERATURE, IID_DEFAULT, RID_SENSOR_VALUE);
        return 0;
    }
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static const anjay_dm_object_def_t OBJ_DEF = {
    .oid = OID_TEMPERATURE,
    .version = "1.1",
    .handlers = {
        .list_instances = temp_list_instances,
        .list_resources = temp_list_resources,
        .resource_read = temp_read,
        .resource_execute = temp_execute
    }
};

static const anjay_dm_object_def_t *const OBJ_DEF_PTR = &OBJ_DEF;

const anjay_dm_object_def_t *const *temp_object_def(void) {
    ensure_sample();
    return &OBJ_DEF_PTR;
}

void temp_object_update(anjay_t *anjay) {
    if (!anjay) {
        return;
    }
    TickType_t now = xTaskGetTickCount();
    if (g_last_sample_tick == 0 || (now - g_last_sample_tick) >= pdMS_TO_TICKS(TEMP_SAMPLE_INTERVAL_MS)) {
        g_last_sample_tick = now;
        bool min_changed = false;
        bool max_changed = false;
        float value = read_temperature_sensor();
        bool first = record_sample(value, &min_changed, &max_changed);
        float delta = fabsf(value - g_last_notified);
        bool notify_delta = first || delta >= TEMP_DELTA_EPS;

        ESP_LOGD(TAG, "update: val=%.3fC delta=%.3fC first=%d min=%.3f max=%.3f",
                 value, delta, (int) first, g_min_measured, g_max_measured);

        if (first || notify_delta) {
            g_last_notify_tick = now;
            g_last_notified = value;
            int err = anjay_notify_changed(anjay, OID_TEMPERATURE, IID_DEFAULT, RID_SENSOR_VALUE);
            if (err < 0) {
                ESP_LOGW(TAG, "notify_changed /3303/0/5700 failed: %d (no observers yet or error)", err);
            } else {
                ESP_LOGD(TAG, "notify_changed /3303/0/5700 queued");
            }
        }
        if (min_changed) {
            int err = anjay_notify_changed(anjay, OID_TEMPERATURE, IID_DEFAULT, RID_MIN_MEASURED);
            if (err < 0) {
                ESP_LOGW(TAG, "notify_changed /3303/0/5601 failed: %d", err);
            } else {
                ESP_LOGD(TAG, "notify_changed /3303/0/5601 queued");
            }
        }
        if (max_changed) {
            int err = anjay_notify_changed(anjay, OID_TEMPERATURE, IID_DEFAULT, RID_MAX_MEASURED);
            if (err < 0) {
                ESP_LOGW(TAG, "notify_changed /3303/0/5602 failed: %d", err);
            } else {
                ESP_LOGD(TAG, "notify_changed /3303/0/5602 queued");
            }
        }
    }
}
