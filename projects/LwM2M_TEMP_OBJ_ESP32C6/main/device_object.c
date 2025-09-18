#include "device_object.h"
#include "sdkconfig.h"

#include <anjay/anjay.h>
#include <anjay/io.h>
#include <avsystem/commons/avs_memory.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_system.h>
#include <esp_random.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>

#define RID_MANUFACTURER 0
#define RID_MODEL_NUMBER 1
#define RID_SERIAL_NUMBER 2
#define RID_FIRMWARE_VERSION 3
#define RID_REBOOT 4
#define RID_ERROR_CODE 11
#define RID_CURRENT_TIME 13
#define RID_UTC_OFFSET 14
#define RID_TIMEZONE 15
#define RID_SUPPORTED_BINDING_AND_MODES 16
#define RID_DEVICE_TYPE 17
#define RID_HARDWARE_VERSION 18
#define RID_SOFTWARE_VERSION 19
#define RID_BATTERY_STATUS 20
#define RID_MEMORY_TOTAL 21
#define RID_BATTERY_LEVEL 9
#define RID_MEMORY_FREE 10
#define RID_POWER_SOURCE_VOLTAGE 7
#define RID_POWER_SOURCE_CURRENT 8

#define DEVICE_UPDATE_PERIOD_MS 5000
#define DEVICE_MANUFACTURER "Espressif"
#ifndef CONFIG_IDF_TARGET
#define CONFIG_IDF_TARGET "esp32"
#endif
#define DEVICE_MODEL CONFIG_IDF_TARGET
#define DEVICE_TYPE "Demo"

static const char *TAG = "device_obj";

typedef struct device_object_struct {
    const anjay_dm_object_def_t *def;
    char serial_number[64];
    char utc_offset[16];
    char timezone[32];
    int32_t battery_level;
    int32_t battery_status;
    uint32_t memory_free_kb;
    uint32_t memory_total_kb;
    int32_t power_voltage_mv;
    int32_t power_current_ma;
    bool do_reboot;
    TickType_t last_update_tick;
} device_object_t;

static inline device_object_t *get_obj(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return AVS_CONTAINER_OF(obj_ptr, device_object_t, def);
}

static int list_resources(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid,
                          anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay; (void) obj_ptr; (void) iid;
    anjay_dm_emit_res(ctx, RID_MANUFACTURER, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_MODEL_NUMBER, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_SERIAL_NUMBER, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_FIRMWARE_VERSION, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_REBOOT, ANJAY_DM_RES_E, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_ERROR_CODE, ANJAY_DM_RES_RM, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_CURRENT_TIME, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_UTC_OFFSET, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_TIMEZONE, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_SUPPORTED_BINDING_AND_MODES, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_DEVICE_TYPE, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_HARDWARE_VERSION, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_SOFTWARE_VERSION, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_BATTERY_STATUS, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_MEMORY_TOTAL, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_BATTERY_LEVEL, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_MEMORY_FREE, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_POWER_SOURCE_VOLTAGE, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_POWER_SOURCE_CURRENT, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    return 0;
}

static int list_resource_instances(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid,
                                   anjay_dm_list_ctx_t *ctx) {
    (void) anjay; (void) obj_ptr; (void) iid;
    switch (rid) {
    case RID_ERROR_CODE:
        anjay_dm_emit(ctx, 0);
        return 0;
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int64_t get_current_time_seconds(void) {
    time_t now = 0;
    if (time(&now) != (time_t) -1 && now > 0) {
        return (int64_t) now;
    }
    return (int64_t) (esp_timer_get_time() / 1000000ULL);
}

static int32_t clamp_i32(int32_t value, int32_t min, int32_t max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static void refresh_dynamic_metrics(device_object_t *obj) {
    int32_t delta = (int32_t) (esp_random() % 11) - 5;
    obj->battery_level = clamp_i32(obj->battery_level + delta, 5, 100);
    if (obj->battery_level > 80) {
        obj->battery_status = 0; // NORMAL
    } else if (obj->battery_level > 40) {
        obj->battery_status = (delta < 0) ? 2 : 1; // DISCHARGING / CHARGING
    } else {
        obj->battery_status = 4; // NEED_REPLACEMENT
    }

    obj->memory_free_kb = heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024;

    int32_t volt_delta = (int32_t) (esp_random() % 101) - 50;
    obj->power_voltage_mv = clamp_i32(obj->power_voltage_mv + volt_delta, 3600, 4200);

    int32_t curr_delta = (int32_t) (esp_random() % 23) - 11;
    obj->power_current_ma = clamp_i32(obj->power_current_ma + curr_delta, 50, 220);
}

static int resource_read(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj_ptr,
                         anjay_iid_t iid,
                         anjay_rid_t rid,
                         anjay_riid_t riid,
                         anjay_output_ctx_t *ctx) {
    (void) anjay;
    device_object_t *obj = get_obj(obj_ptr);
    assert(obj);
    (void) iid;
    switch (rid) {
    case RID_MANUFACTURER:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, DEVICE_MANUFACTURER);
    case RID_MODEL_NUMBER:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, DEVICE_MODEL);
    case RID_SERIAL_NUMBER:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, obj->serial_number);
    case RID_FIRMWARE_VERSION:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, esp_get_idf_version());
    case RID_ERROR_CODE:
        assert(riid == 0);
        return anjay_ret_i32(ctx, 0);
    case RID_SUPPORTED_BINDING_AND_MODES:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, "U");
    case RID_SOFTWARE_VERSION:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, esp_get_idf_version());
    case RID_DEVICE_TYPE:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, DEVICE_TYPE);
    case RID_HARDWARE_VERSION:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, DEVICE_MODEL);
    case RID_BATTERY_LEVEL:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i32(ctx, obj->battery_level);
    case RID_MEMORY_FREE:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i64(ctx, (int64_t) obj->memory_free_kb);
    case RID_CURRENT_TIME:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i64(ctx, get_current_time_seconds());
    case RID_UTC_OFFSET:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, obj->utc_offset);
    case RID_TIMEZONE:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, obj->timezone);
    case RID_BATTERY_STATUS:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i32(ctx, obj->battery_status);
    case RID_MEMORY_TOTAL:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i64(ctx, (int64_t) obj->memory_total_kb);
    case RID_POWER_SOURCE_VOLTAGE:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i32(ctx, obj->power_voltage_mv);
    case RID_POWER_SOURCE_CURRENT:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i32(ctx, obj->power_current_ma);
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int resource_write(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid,
                          anjay_rid_t rid,
                          anjay_riid_t riid,
                          anjay_input_ctx_t *ctx) {
    (void) anjay; (void) iid; (void) riid;
    device_object_t *obj = get_obj(obj_ptr);
    assert(obj);
    switch (rid) {
    case RID_UTC_OFFSET: {
        char buffer[sizeof(obj->utc_offset)];
        int result = anjay_get_string(ctx, buffer, sizeof(buffer));
        if (!result) {
            strlcpy(obj->utc_offset, buffer, sizeof(obj->utc_offset));
        }
        return result;
    }
    case RID_TIMEZONE: {
        char buffer[sizeof(obj->timezone)];
        int result = anjay_get_string(ctx, buffer, sizeof(buffer));
        if (!result) {
            strlcpy(obj->timezone, buffer, sizeof(obj->timezone));
        }
        return result;
    }
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int resource_execute(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            anjay_execute_ctx_t *arg_ctx) {
    (void) arg_ctx;
    device_object_t *obj = get_obj(obj_ptr);
    assert(obj);
    (void) iid;
    switch (rid) {
    case RID_REBOOT:
        obj->do_reboot = true;
        return 0;
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static const anjay_dm_object_def_t OBJ_DEF = {
    .oid = 3,
    .version = "1.2",
    .handlers = {
        .list_instances = anjay_dm_list_instances_SINGLE,
        .list_resources = list_resources,
        .list_resource_instances = list_resource_instances,
        .resource_read = resource_read,
        .resource_write = resource_write,
        .resource_execute = resource_execute,
        .transaction_begin = anjay_dm_transaction_NOOP,
        .transaction_validate = anjay_dm_transaction_NOOP,
        .transaction_commit = anjay_dm_transaction_NOOP,
        .transaction_rollback = anjay_dm_transaction_NOOP
    }
};

const anjay_dm_object_def_t **device_object_create(const char *endpoint_name) {
    device_object_t *obj = (device_object_t *) avs_calloc(1, sizeof(device_object_t));
    if (!obj) {
        return NULL;
    }
    obj->def = &OBJ_DEF;
    if (endpoint_name && *endpoint_name) {
        strlcpy(obj->serial_number, endpoint_name, sizeof(obj->serial_number));
    } else {
        strlcpy(obj->serial_number, "esp32-device", sizeof(obj->serial_number));
    }
    strlcpy(obj->utc_offset, "+00:00", sizeof(obj->utc_offset));
    strlcpy(obj->timezone, "UTC", sizeof(obj->timezone));
    obj->battery_level = 95;
    obj->battery_status = 0;
    obj->memory_total_kb = heap_caps_get_total_size(MALLOC_CAP_8BIT) / 1024;
    obj->memory_free_kb = heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024;
    obj->power_voltage_mv = 3900;
    obj->power_current_ma = 120;
    obj->last_update_tick = xTaskGetTickCount();
    ESP_LOGI(TAG, "Device(3) instance initialized");
    return &obj->def;
}

void device_object_release(const anjay_dm_object_def_t **def) {
    if (def) {
        device_object_t *obj = get_obj(def);
        avs_free(obj);
    }
}

void device_object_update(anjay_t *anjay, const anjay_dm_object_def_t *const *def) {
    if (!anjay || !def) {
        return;
    }
    device_object_t *obj = get_obj(def);
    if (obj->do_reboot) {
        ESP_LOGW(TAG, "Reboot requested via LwM2M");
        esp_system_abort("Rebooting ...");
    }
    TickType_t now = xTaskGetTickCount();
    if ((now - obj->last_update_tick) >= pdMS_TO_TICKS(DEVICE_UPDATE_PERIOD_MS)) {
        obj->last_update_tick = now;
        refresh_dynamic_metrics(obj);
        anjay_notify_changed(anjay, 3, 0, RID_BATTERY_LEVEL);
        anjay_notify_changed(anjay, 3, 0, RID_MEMORY_FREE);
        anjay_notify_changed(anjay, 3, 0, RID_BATTERY_STATUS);
        anjay_notify_changed(anjay, 3, 0, RID_CURRENT_TIME);
        anjay_notify_changed(anjay, 3, 0, RID_POWER_SOURCE_VOLTAGE);
        anjay_notify_changed(anjay, 3, 0, RID_POWER_SOURCE_CURRENT);
    }
}
