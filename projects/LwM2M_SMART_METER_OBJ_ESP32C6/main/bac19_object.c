#include "bac19_object.h"
#include <string.h>
#include <stdlib.h>
#include <esp_log.h>
#include <anjay/io.h>
#include <stdint.h>

#define OID_BAC 19
#define RID_DATA 0
#define RID_DATA_CT 2
#define RID_DATA_DESC 3
#define RID_DATA_FMT 4
#define RID_APP_ID 5

#define IID_FW 65533
#define IID_SW 65534

static const char *TAG_BAC = "bac19";

typedef struct blob_buf_s {
    uint8_t *ptr;
    size_t size;
    size_t capacity;
} blob_buf_t;

typedef struct entry_s {
    anjay_iid_t iid;
    blob_buf_t data;
    char *desc;
    char *fmt;
    char *appid;
    int64_t ctime;
} entry_t;

typedef struct {
    const anjay_dm_object_def_t *def;
    entry_t fw;
    entry_t sw;
} bac_ctx_t;

static int list_instances(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                          anjay_dm_list_ctx_t *ctx) {
    (void) anjay; (void) def;
    anjay_dm_emit(ctx, IID_FW);
    anjay_dm_emit(ctx, IID_SW);
    return 0;
}

static entry_t *find_entry(bac_ctx_t *ctx, anjay_iid_t iid) {
    if (iid == IID_FW) return &ctx->fw;
    if (iid == IID_SW) return &ctx->sw;
    return NULL;
}

static const entry_t *find_entry_const(const bac_ctx_t *ctx, anjay_iid_t iid) {
    return find_entry((bac_ctx_t *) ctx, iid);
}

static int list_resources(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                          anjay_iid_t iid, anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay; (void) def; (void) iid;
    anjay_dm_emit_res(ctx, RID_DATA, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_DATA_CT, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_DATA_DESC, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_DATA_FMT, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_APP_ID, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    return 0;
}

static int bac_read(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                anjay_iid_t iid, anjay_rid_t rid, anjay_riid_t riid, anjay_output_ctx_t *ctx) {
    (void) anjay; (void) riid;
    const bac_ctx_t *obj = AVS_CONTAINER_OF(def, bac_ctx_t, def);
    const entry_t *e = find_entry_const(obj, iid);
    if (!e) return ANJAY_ERR_NOT_FOUND;
    switch (rid) {
    case RID_DATA:
        if (e->data.ptr && e->data.size > 0) return anjay_ret_bytes(ctx, e->data.ptr, e->data.size);
        return anjay_ret_bytes(ctx, "", 0);
    case RID_DATA_CT:
        return anjay_ret_i64(ctx, e->ctime);
    case RID_DATA_DESC:
        return anjay_ret_string(ctx, e->desc ? e->desc : "");
    case RID_DATA_FMT:
        return anjay_ret_string(ctx, e->fmt ? e->fmt : "");
    case RID_APP_ID:
        return anjay_ret_string(ctx, e->appid ? e->appid : "");
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int bac_write(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                 anjay_iid_t iid, anjay_rid_t rid, anjay_riid_t riid,
                 anjay_input_ctx_t *ctx) {
    (void) anjay; (void) riid;
    bac_ctx_t *obj = AVS_CONTAINER_OF(def, bac_ctx_t, def);
    entry_t *e = find_entry(obj, iid);
    if (!e) return ANJAY_ERR_NOT_FOUND;
    switch (rid) {
    case RID_DATA: {
        if (e->data.ptr) { free(e->data.ptr); e->data.ptr = NULL; e->data.size = 0; e->data.capacity = 0; }
        uint8_t buf[512];
        bool finished = false;
        do {
            size_t bytes_read = 0;
            int res = anjay_get_bytes(ctx, &bytes_read, &finished, buf, sizeof(buf));
            if (res) return res;
            if (bytes_read > 0) {
                if (e->data.size + bytes_read > e->data.capacity) {
                    size_t new_cap = e->data.capacity ? e->data.capacity : 512;
                    while (new_cap < e->data.size + bytes_read) new_cap *= 2;
                    void *np = realloc(e->data.ptr, new_cap);
                    if (!np) return ANJAY_ERR_INTERNAL;
                    e->data.ptr = (uint8_t *) np;
                    e->data.capacity = new_cap;
                }
                memcpy(e->data.ptr + e->data.size, buf, bytes_read);
                e->data.size += bytes_read;
            }
        } while (!finished);
        ESP_LOGI(TAG_BAC, "BAC19[%u]: received %zu bytes", (unsigned) iid, (size_t) e->data.size);
        return 0;
    }
    case RID_DATA_CT: {
        int64_t v = 0; int res = anjay_get_i64(ctx, &v); if (!res) e->ctime = v; return res; }
    case RID_DATA_DESC: {
        char tmp[128]; int res = anjay_get_string(ctx, tmp, sizeof(tmp)); if (!res) { free(e->desc); e->desc = strdup(tmp);} return res; }
    case RID_DATA_FMT: {
        char tmp[64]; int res = anjay_get_string(ctx, tmp, sizeof(tmp)); if (!res) { free(e->fmt); e->fmt = strdup(tmp);} return res; }
    case RID_APP_ID: {
        char tmp[64]; int res = anjay_get_string(ctx, tmp, sizeof(tmp)); if (!res) { free(e->appid); e->appid = strdup(tmp);} return res; }
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static const anjay_dm_object_def_t OBJ_DEF = {
    .oid = OID_BAC,
    .version = "1.0",
    .handlers = {
        .list_instances = list_instances,
        .list_resources = list_resources,
        .resource_read = bac_read,
        .resource_write = bac_write
    }
};

static bac_ctx_t g_bac = {
    .def = &OBJ_DEF,
    .fw = { .iid = IID_FW, .data = { .ptr=NULL, .size=0, .capacity=0 }, .desc=NULL, .fmt=NULL, .appid=NULL, .ctime=0 },
    .sw = { .iid = IID_SW, .data = { .ptr=NULL, .size=0, .capacity=0 }, .desc=NULL, .fmt=NULL, .appid=NULL, .ctime=0 }
};

const anjay_dm_object_def_t **bac19_object_create(void) {
    ESP_LOGI(TAG_BAC, "BAC(19) created with instances %u and %u", (unsigned) IID_FW, (unsigned) IID_SW);
    return &g_bac.def;
}

void bac19_object_release(const anjay_dm_object_def_t *const *def) {
    (void) def;
    if (g_bac.fw.data.ptr) { free(g_bac.fw.data.ptr); g_bac.fw.data.ptr=NULL; g_bac.fw.data.size=g_bac.fw.data.capacity=0; }
    if (g_bac.sw.data.ptr) { free(g_bac.sw.data.ptr); g_bac.sw.data.ptr=NULL; g_bac.sw.data.size=g_bac.sw.data.capacity=0; }
    free(g_bac.fw.desc); g_bac.fw.desc=NULL;
    free(g_bac.sw.desc); g_bac.sw.desc=NULL;
    free(g_bac.fw.fmt); g_bac.fw.fmt=NULL;
    free(g_bac.sw.fmt); g_bac.sw.fmt=NULL;
    free(g_bac.fw.appid); g_bac.fw.appid=NULL;
    free(g_bac.sw.appid); g_bac.sw.appid=NULL;
}
