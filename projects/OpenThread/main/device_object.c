#include "device_object.h"
#include <anjay/anjay.h>
#include <anjay/io.h>
#include <avsystem/commons/avs_memory.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_idf_version.h>
#include <string.h>

#define RID_MANUFACTURER 0
#define RID_MODEL_NUMBER 1
#define RID_SERIAL_NUMBER 2
#define RID_FIRMWARE_VERSION 3
#define RID_REBOOT 4

static const char *TAG = "device_obj";

typedef struct {
    const anjay_dm_object_def_t *def;
    char serial[64];
    bool reboot_requested;
} device_object_t;

static int list_resources(anjay_t *anjay, const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid, anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay; (void) obj_ptr; (void) iid;
    anjay_dm_emit_res(ctx, RID_MANUFACTURER, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_MODEL_NUMBER, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_SERIAL_NUMBER, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_FIRMWARE_VERSION, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_REBOOT, ANJAY_DM_RES_E, ANJAY_DM_RES_PRESENT);
    return 0;
}

static int resource_read(anjay_t *anjay, const anjay_dm_object_def_t *const *obj_ptr,
                         anjay_iid_t iid, anjay_rid_t rid, anjay_riid_t riid,
                         anjay_output_ctx_t *ctx) {
    (void) anjay; (void) iid; (void) riid;
    device_object_t *obj = AVS_CONTAINER_OF(obj_ptr, device_object_t, def);
    
    switch (rid) {
    case RID_MANUFACTURER:
        return anjay_ret_string(ctx, "Espressif");
    case RID_MODEL_NUMBER:
        return anjay_ret_string(ctx, "ESP32-C6-DevKitC");
    case RID_SERIAL_NUMBER:
        return anjay_ret_string(ctx, obj->serial);
    case RID_FIRMWARE_VERSION:
        return anjay_ret_string(ctx, esp_get_idf_version());
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int resource_execute(anjay_t *anjay, const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid, anjay_rid_t rid,
                            anjay_execute_ctx_t *ctx) {
    (void) anjay; (void) iid; (void) ctx;
    device_object_t *obj = AVS_CONTAINER_OF(obj_ptr, device_object_t, def);
    
    if (rid == RID_REBOOT) {
        ESP_LOGI(TAG, "Reboot requested by LwM2M server");
        obj->reboot_requested = true;
        return 0;
    }
    return ANJAY_ERR_METHOD_NOT_ALLOWED;
}

static const anjay_dm_object_def_t OBJ_DEF = {
    .oid = 3,
    .version = "1.2",
    .handlers = {
        .list_instances = anjay_dm_list_instances_SINGLE,
        .list_resources = list_resources,
        .resource_read = resource_read,
        .resource_execute = resource_execute,
    }
};

const anjay_dm_object_def_t **device_object_create(const char *endpoint_name) {
    device_object_t *obj = (device_object_t *) avs_calloc(1, sizeof(device_object_t));
    if (!obj) {
        return NULL;
    }
    
    obj->def = &OBJ_DEF;
    snprintf(obj->serial, sizeof(obj->serial), "%s", endpoint_name ? endpoint_name : "UNKNOWN");
    obj->reboot_requested = false;
    
    return &obj->def;
}

void device_object_release(const anjay_dm_object_def_t **def) {
    if (def && *def) {
        device_object_t *obj = AVS_CONTAINER_OF(def, device_object_t, def);
        avs_free(obj);
    }
}

void device_object_update(anjay_t *anjay, const anjay_dm_object_def_t *const *def) {
    (void) anjay;
    if (!def || !*def) {
        return;
    }
    
    device_object_t *obj = AVS_CONTAINER_OF(def, device_object_t, def);
    if (obj->reboot_requested) {
        ESP_LOGI(TAG, "Rebooting system in 2 seconds...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }
}
