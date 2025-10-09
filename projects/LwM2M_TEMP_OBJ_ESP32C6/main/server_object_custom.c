#include "server_object_custom.h"
#include <anjay/anjay.h>
#include <anjay/io.h>
#include <avsystem/commons/avs_memory.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include <esp_log.h>

// LwM2M Server Object (OID: 1) Resource IDs
#define RID_SHORT_SERVER_ID 0
#define RID_LIFETIME 1
#define RID_DEFAULT_MIN_PERIOD 2
#define RID_DEFAULT_MAX_PERIOD 3
#define RID_DISABLE 4
#define RID_DISABLE_TIMEOUT 5
#define RID_NOTIFICATION_STORING 6
#define RID_BINDING 7
#define RID_REGISTRATION_UPDATE_TRIGGER 8

static const char *TAG = "server_obj_custom";

typedef struct server_object_custom_struct {
    const anjay_dm_object_def_t *def;
    anjay_ssid_t ssid;
    int32_t lifetime;
    int32_t default_min_period;
    int32_t default_max_period;
    bool notification_storing;
    char binding[8];
    bool disabled;
    int32_t disable_timeout;
} server_object_custom_t;

static inline server_object_custom_t *get_obj(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return AVS_CONTAINER_OF(obj_ptr, server_object_custom_t, def);
}

static int list_resources(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid,
                          anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay; (void) obj_ptr; (void) iid;
    
    // Resources in ascending order
    anjay_dm_emit_res(ctx, RID_SHORT_SERVER_ID,           ANJAY_DM_RES_R,  ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_LIFETIME,                  ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_DEFAULT_MIN_PERIOD,        ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_DEFAULT_MAX_PERIOD,        ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_DISABLE,                   ANJAY_DM_RES_E,  ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_DISABLE_TIMEOUT,           ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_NOTIFICATION_STORING,      ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_BINDING,                   ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_REGISTRATION_UPDATE_TRIGGER, ANJAY_DM_RES_E, ANJAY_DM_RES_PRESENT);
    
    return 0;
}

static int resource_read(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj_ptr,
                         anjay_iid_t iid,
                         anjay_rid_t rid,
                         anjay_riid_t riid,
                         anjay_output_ctx_t *ctx) {
    (void) anjay; (void) iid;
    server_object_custom_t *obj = get_obj(obj_ptr);
    assert(obj);
    assert(riid == ANJAY_ID_INVALID);
    
    switch (rid) {
    case RID_SHORT_SERVER_ID:
        return anjay_ret_i32(ctx, (int32_t) obj->ssid);
    case RID_LIFETIME:
        return anjay_ret_i32(ctx, obj->lifetime);
    case RID_DEFAULT_MIN_PERIOD:
        return anjay_ret_i32(ctx, obj->default_min_period);
    case RID_DEFAULT_MAX_PERIOD:
        return anjay_ret_i32(ctx, obj->default_max_period);
    case RID_DISABLE_TIMEOUT:
        return anjay_ret_i32(ctx, obj->disable_timeout);
    case RID_NOTIFICATION_STORING:
        return anjay_ret_bool(ctx, obj->notification_storing);
    case RID_BINDING:
        return anjay_ret_string(ctx, obj->binding);
    default:
        ESP_LOGW(TAG, "Unhandled Server resource read RID=%d", rid);
        return ANJAY_ERR_NOT_FOUND;
    }
}

static int resource_write(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid,
                          anjay_rid_t rid,
                          anjay_riid_t riid,
                          anjay_input_ctx_t *ctx) {
    (void) anjay; (void) iid; (void) riid;
    server_object_custom_t *obj = get_obj(obj_ptr);
    assert(obj);
    
    switch (rid) {
    case RID_LIFETIME: {
        int32_t value;
        int result = anjay_get_i32(ctx, &value);
        if (!result && value > 0) {
            obj->lifetime = value;
            ESP_LOGI(TAG, "Lifetime updated to %ld seconds", (long)value);
        }
        return result;
    }
    case RID_DEFAULT_MIN_PERIOD: {
        int32_t value;
        int result = anjay_get_i32(ctx, &value);
        if (!result && value >= 0) {
            obj->default_min_period = value;
        }
        return result;
    }
    case RID_DEFAULT_MAX_PERIOD: {
        int32_t value;
        int result = anjay_get_i32(ctx, &value);
        if (!result && value >= 0) {
            obj->default_max_period = value;
        }
        return result;
    }
    case RID_DISABLE_TIMEOUT: {
        int32_t value;
        int result = anjay_get_i32(ctx, &value);
        if (!result && value >= 0) {
            obj->disable_timeout = value;
        }
        return result;
    }
    case RID_NOTIFICATION_STORING: {
        bool value;
        int result = anjay_get_bool(ctx, &value);
        if (!result) {
            obj->notification_storing = value;
        }
        return result;
    }
    case RID_BINDING: {
        char buffer[sizeof(obj->binding)];
        int result = anjay_get_string(ctx, buffer, sizeof(buffer));
        if (!result) {
            strlcpy(obj->binding, buffer, sizeof(obj->binding));
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
    (void) arg_ctx; (void) iid;
    server_object_custom_t *obj = get_obj(obj_ptr);
    assert(obj);
    
    switch (rid) {
    case RID_DISABLE:
        ESP_LOGW(TAG, "Server Disable executed - not fully implemented");
        obj->disabled = true;
        return 0;
    case RID_REGISTRATION_UPDATE_TRIGGER:
        ESP_LOGI(TAG, "Registration Update triggered");
        // Trigger registration update
        if (anjay_schedule_registration_update(anjay, obj->ssid) != 0) {
            ESP_LOGE(TAG, "Failed to schedule registration update");
            return ANJAY_ERR_INTERNAL;
        }
        return 0;
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static const anjay_dm_object_def_t OBJ_DEF = {
    .oid = 1,
    .version = "1.2",  // ← VERSIÓN 1.2 (incluye RID 2 y 3)
    .handlers = {
        .list_instances = anjay_dm_list_instances_SINGLE,
        .list_resources = list_resources,
        .resource_read = resource_read,
        .resource_write = resource_write,
        .resource_execute = resource_execute,
        .transaction_begin = anjay_dm_transaction_NOOP,
        .transaction_validate = anjay_dm_transaction_NOOP,
        .transaction_commit = anjay_dm_transaction_NOOP,
        .transaction_rollback = anjay_dm_transaction_NOOP
    }
};

const anjay_dm_object_def_t **server_object_custom_create(
    anjay_ssid_t ssid,
    int32_t lifetime,
    int32_t default_min_period,
    int32_t default_max_period,
    const char *binding) {
    
    server_object_custom_t *obj = (server_object_custom_t *) avs_calloc(1, sizeof(server_object_custom_t));
    if (!obj) {
        return NULL;
    }
    
    obj->def = &OBJ_DEF;
    obj->ssid = ssid;
    obj->lifetime = lifetime;
    obj->default_min_period = default_min_period;
    obj->default_max_period = default_max_period;
    obj->notification_storing = true;
    obj->disabled = false;
    obj->disable_timeout = 86400; // 24 hours default
    
    if (binding && *binding) {
        strlcpy(obj->binding, binding, sizeof(obj->binding));
    } else {
        strlcpy(obj->binding, "U", sizeof(obj->binding));
    }
    
    ESP_LOGI(TAG, "Server(1) v1.2 instance initialized (SSID=%d, Lifetime=%ld)", (int)ssid, (long)lifetime);
    return &obj->def;
}

void server_object_custom_release(const anjay_dm_object_def_t **def) {
    if (def) {
        server_object_custom_t *obj = get_obj(def);
        avs_free(obj);
    }
}

void server_object_custom_update(anjay_t *anjay, const anjay_dm_object_def_t *const *def) {
    (void) anjay;
    (void) def;
    // No periodic updates needed for basic Server object
}
