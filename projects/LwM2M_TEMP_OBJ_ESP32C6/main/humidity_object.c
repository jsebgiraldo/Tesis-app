#include "humidity_object.h"

#include <math.h>
#include <stdbool.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <anjay/io.h>

#define OID_HUMIDITY 3304
#define IID_DEFAULT 0
#define RID_SENSOR_VALUE 5700
#define RID_SENSOR_UNITS 5701
#define RID_MIN_MEASURED 5601
#define RID_MAX_MEASURED 5602
#define RID_RESET_MIN_MAX 5605

#define HUM_NOTIFY_MIN_INTERVAL_MS 20000
#define HUM_NOTIFY_MIN_DELTA_PCT 1.0f

static float read_humidity_sensor(void) {
    TickType_t ticks = xTaskGetTickCount();
    float base = 55.0f;
    float phase = (float) (ticks % 20000) / 700.0f;
    float delta = 10.0f * sinf(phase);
    return base + delta;
}

static bool g_have_value = false;
static float g_current_value = 0.0f;
static float g_min_measured = 0.0f;
static float g_max_measured = 0.0f;
static float g_last_notified = 0.0f;
static TickType_t g_last_notify_tick = 0;

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
        (void) record_sample(read_humidity_sensor(), NULL, NULL);
        g_last_notified = g_current_value;
        g_last_notify_tick = xTaskGetTickCount();
    }
}

static int hum_list_instances(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *def,
                              anjay_dm_list_ctx_t *ctx) {
    (void) anjay; (void) def;
    anjay_dm_emit(ctx, IID_DEFAULT);
    return 0;
}

static int hum_list_resources(anjay_t *anjay,
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

static int hum_read(anjay_t *anjay,
                    const anjay_dm_object_def_t *const *def,
                    anjay_iid_t iid,
                    anjay_rid_t rid,
                    anjay_riid_t riid,
                    anjay_output_ctx_t *ctx) {
    (void) anjay; (void) def; (void) iid; (void) riid;
    ensure_sample();
    switch (rid) {
    case RID_SENSOR_VALUE:
        return anjay_ret_float(ctx, g_current_value);
    case RID_SENSOR_UNITS:
        return anjay_ret_string(ctx, "%RH");
    case RID_MIN_MEASURED:
        return anjay_ret_float(ctx, g_min_measured);
    case RID_MAX_MEASURED:
        return anjay_ret_float(ctx, g_max_measured);
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int hum_execute(anjay_t *anjay,
                       const anjay_dm_object_def_t *const *def,
                       anjay_iid_t iid,
                       anjay_rid_t rid,
                       anjay_execute_ctx_t *arg_ctx) {
    (void) def; (void) iid; (void) arg_ctx;
    switch (rid) {
    case RID_RESET_MIN_MAX: {
        float value = read_humidity_sensor();
        g_have_value = false;
        (void) record_sample(value, NULL, NULL);
        g_last_notified = value;
        g_last_notify_tick = xTaskGetTickCount();
        anjay_notify_changed(anjay, OID_HUMIDITY, IID_DEFAULT, RID_MIN_MEASURED);
        anjay_notify_changed(anjay, OID_HUMIDITY, IID_DEFAULT, RID_MAX_MEASURED);
        anjay_notify_changed(anjay, OID_HUMIDITY, IID_DEFAULT, RID_SENSOR_VALUE);
        return 0;
    }
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static const anjay_dm_object_def_t OBJ_DEF = {
    .oid = OID_HUMIDITY,
    .handlers = {
        .list_instances = hum_list_instances,
        .list_resources = hum_list_resources,
        .resource_read = hum_read,
        .resource_execute = hum_execute
    }
};

static const anjay_dm_object_def_t *const OBJ_DEF_PTR = &OBJ_DEF;

const anjay_dm_object_def_t *const *humidity_object_def(void) {
    ensure_sample();
    return &OBJ_DEF_PTR;
}

void humidity_object_update(anjay_t *anjay) {
    if (!anjay) {
        return;
    }
    bool min_changed = false;
    bool max_changed = false;
    float value = read_humidity_sensor();
    bool first = record_sample(value, &min_changed, &max_changed);

    TickType_t now = xTaskGetTickCount();
    bool notify_time = g_last_notify_tick
            && (now - g_last_notify_tick >= pdMS_TO_TICKS(HUM_NOTIFY_MIN_INTERVAL_MS));
    bool notify_delta = first || fabsf(value - g_last_notified) >= HUM_NOTIFY_MIN_DELTA_PCT;

    if (first || notify_delta || notify_time) {
        g_last_notify_tick = now;
        g_last_notified = value;
        anjay_notify_changed(anjay, OID_HUMIDITY, IID_DEFAULT, RID_SENSOR_VALUE);
    }
    if (min_changed) {
        anjay_notify_changed(anjay, OID_HUMIDITY, IID_DEFAULT, RID_MIN_MEASURED);
    }
    if (max_changed) {
        anjay_notify_changed(anjay, OID_HUMIDITY, IID_DEFAULT, RID_MAX_MEASURED);
    }
}
