#include "temp_object.h"
#include <anjay/io.h>
#include <esp_log.h>
#include <esp_random.h>
#include <math.h>

#define OID_TEMPERATURE 3303
#define RID_SENSOR_VALUE 5700
#define RID_MIN_MEASURED_VALUE 5601
#define RID_MAX_MEASURED_VALUE 5602
#define RID_MIN_RANGE_VALUE 5603
#define RID_MAX_RANGE_VALUE 5604
#define RID_SENSOR_UNITS 5701
#define RID_RESET_MIN_MAX 5605

static const char *TAG = "temp_obj";

typedef struct {
    const anjay_dm_object_def_t *def;
    float sensor_value;
    float min_measured;
    float max_measured;
    float min_range;
    float max_range;
} temp_ctx_t;

static temp_ctx_t g_temp = {
    .sensor_value = 22.5f,
    .min_measured = 20.0f,
    .max_measured = 25.0f,
    .min_range = -40.0f,
    .max_range = 85.0f
};

static int list_instances(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                          anjay_dm_list_ctx_t *ctx) {
    (void) anjay; (void) def;
    anjay_dm_emit(ctx, 0);
    return 0;
}

static int list_resources(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                          anjay_iid_t iid, anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay; (void) def; (void) iid;
    anjay_dm_emit_res(ctx, RID_SENSOR_VALUE, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_MIN_MEASURED_VALUE, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_MAX_MEASURED_VALUE, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_MIN_RANGE_VALUE, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_MAX_RANGE_VALUE, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_SENSOR_UNITS, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_RESET_MIN_MAX, ANJAY_DM_RES_E, ANJAY_DM_RES_PRESENT);
    return 0;
}

static int resource_read(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                         anjay_iid_t iid, anjay_rid_t rid, anjay_riid_t riid,
                         anjay_output_ctx_t *ctx) {
    (void) anjay; (void) iid; (void) riid;
    const temp_ctx_t *obj = AVS_CONTAINER_OF(def, temp_ctx_t, def);
    
    switch (rid) {
    case RID_SENSOR_VALUE:
        return anjay_ret_double(ctx, (double) obj->sensor_value);
    case RID_MIN_MEASURED_VALUE:
        return anjay_ret_double(ctx, (double) obj->min_measured);
    case RID_MAX_MEASURED_VALUE:
        return anjay_ret_double(ctx, (double) obj->max_measured);
    case RID_MIN_RANGE_VALUE:
        return anjay_ret_double(ctx, (double) obj->min_range);
    case RID_MAX_RANGE_VALUE:
        return anjay_ret_double(ctx, (double) obj->max_range);
    case RID_SENSOR_UNITS:
        return anjay_ret_string(ctx, "Cel");
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int resource_execute(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                            anjay_iid_t iid, anjay_rid_t rid,
                            anjay_execute_ctx_t *ctx) {
    (void) anjay; (void) iid; (void) ctx;
    temp_ctx_t *obj = AVS_CONTAINER_OF(def, temp_ctx_t, def);
    
    if (rid == RID_RESET_MIN_MAX) {
        obj->min_measured = obj->sensor_value;
        obj->max_measured = obj->sensor_value;
        ESP_LOGI(TAG, "Min/Max reset to current value: %.1f", obj->sensor_value);
        return 0;
    }
    return ANJAY_ERR_METHOD_NOT_ALLOWED;
}

static const anjay_dm_object_def_t OBJ_DEF = {
    .oid = OID_TEMPERATURE,
    .version = "1.1",
    .handlers = {
        .list_instances = list_instances,
        .list_resources = list_resources,
        .resource_read = resource_read,
        .resource_execute = resource_execute,
    }
};

const anjay_dm_object_def_t *const *temp_object_def(void) {
    g_temp.def = &OBJ_DEF;
    return &g_temp.def;
}

void temp_object_set_value(float temp) {
    g_temp.sensor_value = temp;
    if (temp < g_temp.min_measured) {
        g_temp.min_measured = temp;
    }
    if (temp > g_temp.max_measured) {
        g_temp.max_measured = temp;
    }
}

void temp_object_update(anjay_t *anjay) {
    if (!anjay) {
        return;
    }
    // Simulate temperature variation
    float variation = ((esp_random() % 100) - 50) / 100.0f; // -0.5 to +0.5
    g_temp.sensor_value += variation;
    
    // Update min/max
    if (g_temp.sensor_value < g_temp.min_measured) {
        g_temp.min_measured = g_temp.sensor_value;
    }
    if (g_temp.sensor_value > g_temp.max_measured) {
        g_temp.max_measured = g_temp.sensor_value;
    }
    
    anjay_notify_changed(anjay, OID_TEMPERATURE, 0, RID_SENSOR_VALUE);
    ESP_LOGD(TAG, "Temperature updated: %.1f°C", g_temp.sensor_value);
}
