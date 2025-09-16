#include "connectivity_object.h"\r\n\r\n#include <string.h>\r\n\r\n#include <anjay/io.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include <esp_wifi.h>

#define OID_CONNECTIVITY 4
#define RID_NETWORK_BEARER 0
#define RID_SIGNAL_STRENGTH 2
#define RID_LINK_QUALITY 3

static const char *TAG_CONN = "conn_obj";

typedef struct {
    const anjay_dm_object_def_t *def;
    int32_t signal_strength_dbm;
    int32_t link_quality_pct;
} connectivity_ctx_t;

static inline int32_t clamp_i32(int32_t value, int32_t min, int32_t max) {
    if (value < min) { return min; }
    if (value > max) { return max; }
    return value;
}

static bool update_from_wifi(connectivity_ctx_t *ctx) {
    wifi_ap_record_t ap = { 0 };
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        ctx->signal_strength_dbm = ap.rssi;
        int32_t quality = 0;
        if (ap.rssi <= -100) {
            quality = 0;
        } else if (ap.rssi >= -50) {
            quality = 100;
        } else {
            quality = 2 * (ap.rssi + 100);
        }
        ctx->link_quality_pct = clamp_i32(quality, 0, 100);
        return true;
    }
    return false;
}

static int list_instances(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                          anjay_dm_list_ctx_t *ctx) {
    (void) anjay; (void) def;
    anjay_dm_emit(ctx, 0);
    return 0;
}

static int list_resources(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                          anjay_iid_t iid, anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay; (void) def; (void) iid;
    anjay_dm_emit_res(ctx, RID_NETWORK_BEARER, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_SIGNAL_STRENGTH, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_LINK_QUALITY, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    return 0;
}

static int resource_read(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                         anjay_iid_t iid, anjay_rid_t rid, anjay_riid_t riid,
                         anjay_output_ctx_t *ctx) {
    (void) anjay; (void) iid; (void) riid;
    const connectivity_ctx_t *obj = AVS_CONTAINER_OF(def, connectivity_ctx_t, def);
    switch (rid) {
    case RID_NETWORK_BEARER:
        // 41 = IEEE 802.11 according to LwM2M enum
        return anjay_ret_i32(ctx, 41);
    case RID_SIGNAL_STRENGTH:
        return anjay_ret_i32(ctx, obj->signal_strength_dbm);
    case RID_LINK_QUALITY:
        return anjay_ret_i32(ctx, obj->link_quality_pct);
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static const anjay_dm_object_def_t OBJ_DEF = {
    .oid = OID_CONNECTIVITY,
    .handlers = {
        .list_instances = list_instances,
        .list_resources = list_resources,
        .resource_read = resource_read,
    }
};

static connectivity_ctx_t g_ctx = {
    .def = &OBJ_DEF,
    .signal_strength_dbm = -60,
    .link_quality_pct = 85,
};

const anjay_dm_object_def_t *const *connectivity_object_def(void) {
    return &g_ctx.def;
}

void connectivity_object_update(anjay_t *anjay) {
    if (!anjay) {
        return;
    }
    int32_t old_rssi = g_ctx.signal_strength_dbm;
    int32_t old_quality = g_ctx.link_quality_pct;
    if (!update_from_wifi(&g_ctx)) {
        // decay values slowly if disconnected
        g_ctx.signal_strength_dbm = clamp_i32(g_ctx.signal_strength_dbm - 1, -110, -40);
        g_ctx.link_quality_pct = clamp_i32(g_ctx.link_quality_pct - 2, 0, 100);
    }
    if (g_ctx.signal_strength_dbm != old_rssi) {
        anjay_notify_changed(anjay, OID_CONNECTIVITY, 0, RID_SIGNAL_STRENGTH);
    }
    if (g_ctx.link_quality_pct != old_quality) {
        anjay_notify_changed(anjay, OID_CONNECTIVITY, 0, RID_LINK_QUALITY);
    }
}

