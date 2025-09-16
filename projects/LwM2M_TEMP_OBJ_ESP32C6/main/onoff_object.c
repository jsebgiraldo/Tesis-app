#include "onoff_object.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <anjay/io.h>

#define OID_ONOFF 3312
#define IID_DEFAULT 0
#define RID_ON_OFF 5850

static bool g_on = false;
static TickType_t g_last_toggle_tick = 0;

static int list_instances(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                          anjay_dm_list_ctx_t *ctx) {
    (void) anjay; (void) def;
    anjay_dm_emit(ctx, IID_DEFAULT);
    return 0;
}

static int list_resources(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                          anjay_iid_t iid, anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay; (void) def; (void) iid;
    anjay_dm_emit_res(ctx, RID_ON_OFF, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    return 0;
}

static int resource_read(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                         anjay_iid_t iid, anjay_rid_t rid, anjay_riid_t riid,
                         anjay_output_ctx_t *ctx) {
    (void) anjay; (void) def; (void) iid; (void) riid;
    if (rid == RID_ON_OFF) {
        return anjay_ret_bool(ctx, g_on);
    }
    return ANJAY_ERR_METHOD_NOT_ALLOWED;
}

static int resource_write(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                          anjay_iid_t iid, anjay_rid_t rid, anjay_riid_t riid,
                          anjay_input_ctx_t *ctx) {
    (void) anjay; (void) def; (void) iid; (void) riid;
    if (rid == RID_ON_OFF) {
        bool value = false;
        int result = anjay_get_bool(ctx, &value);
        if (!result && value != g_on) {
            g_on = value;
            anjay_notify_changed(anjay, OID_ONOFF, IID_DEFAULT, RID_ON_OFF);
        }
        return result;
    }
    return ANJAY_ERR_METHOD_NOT_ALLOWED;
}

static const anjay_dm_object_def_t OBJ_DEF = {
    .oid = OID_ONOFF,
    .handlers = {
        .list_instances = list_instances,
        .list_resources = list_resources,
        .resource_read = resource_read,
        .resource_write = resource_write,
    }
};

static const anjay_dm_object_def_t *const OBJ_DEF_PTR = &OBJ_DEF;

const anjay_dm_object_def_t *const *onoff_object_def(void) {
    return &OBJ_DEF_PTR;
}

void onoff_object_set(bool state) {
    g_on = state;
}

void onoff_object_update(anjay_t *anjay) {
    if (!anjay) {
        return;
    }
    TickType_t now = xTaskGetTickCount();
    if ((now - g_last_toggle_tick) >= pdMS_TO_TICKS(30000)) {
        g_last_toggle_tick = now;
        g_on = !g_on;
        anjay_notify_changed(anjay, OID_ONOFF, IID_DEFAULT, RID_ON_OFF);
    }
}
