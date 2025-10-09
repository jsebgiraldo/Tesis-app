// Connectivity Monitoring Object (4) implementation
// Clean file rewrite to remove any stray CR/LF escape sequences that produced
// "extra tokens at end of #include" warnings in earlier builds.

#include "connectivity_object.h"

#include <string.h>
#include <stdio.h>

#include <anjay/io.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_netif.h>

#define OID_CONNECTIVITY 4
#define RID_NETWORK_BEARER 0
#define RID_SIGNAL_STRENGTH 2
#define RID_LINK_QUALITY 3
// LwM2M Connectivity Monitoring (4) additional resources we expose:
// 4: IP Addresses (multiple)
// 5: Router IP Addresses (multiple)
#define RID_IP_ADDRESSES 4
#define RID_ROUTER_IP_ADDRESSES 5

static const char *TAG_CONN = "conn_obj"; // used in update logging

typedef struct {
    const anjay_dm_object_def_t *def;
    int32_t signal_strength_dbm;
    int32_t link_quality_pct;
    // Cached copies of IPs so we can detect change and notify
    char ip_addr[16];      // IPv4 dotted
    char gw_addr[16];      // Gateway IPv4 dotted
    // Smoothing for RSSI
    bool ema_initialized;
    float ema_rssi;
} connectivity_ctx_t;

static inline int32_t clamp_i32(int32_t value, int32_t min, int32_t max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static bool update_from_wifi(connectivity_ctx_t *ctx) {
    wifi_ap_record_t ap = (wifi_ap_record_t){ 0 };
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        // Exponential moving average for stability
        const float alpha = 0.25f; // smoothing factor
        if (!ctx->ema_initialized) {
            ctx->ema_rssi = (float) ap.rssi;
            ctx->ema_initialized = true;
        } else {
            ctx->ema_rssi = alpha * (float) ap.rssi + (1.0f - alpha) * ctx->ema_rssi;
        }
        ctx->signal_strength_dbm = (int32_t) lroundf(ctx->ema_rssi);
        // Map averaged RSSI to link quality percentage
        int32_t q;
        if (ctx->signal_strength_dbm <= -100) {
            q = 0;
        } else if (ctx->signal_strength_dbm >= -50) {
            q = 100;
        } else {
            q = 2 * (ctx->signal_strength_dbm + 100); // linear map -100..-50 => 0..100
        }
        ctx->link_quality_pct = clamp_i32(q, 0, 100);
        return true;
    }
    return false;
}

static int list_instances(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                          anjay_dm_list_ctx_t *ctx) {
    (void) anjay;
    (void) def;
    anjay_dm_emit(ctx, 0);
    return 0;
}

static int list_resources(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                          anjay_iid_t iid, anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    (void) def;
    (void) iid;
    anjay_dm_emit_res(ctx, RID_NETWORK_BEARER, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_SIGNAL_STRENGTH, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_LINK_QUALITY, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_IP_ADDRESSES, ANJAY_DM_RES_RM, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_ROUTER_IP_ADDRESSES, ANJAY_DM_RES_RM, ANJAY_DM_RES_PRESENT);
    return 0;
}

static int list_resource_instances(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                                   anjay_iid_t iid, anjay_rid_t rid,
                                   anjay_dm_list_ctx_t *ctx) {
    (void) anjay;
    (void) def;
    (void) iid;
    switch (rid) {
    case RID_IP_ADDRESSES:
        anjay_dm_emit(ctx, 0); // single IPv4 instance index 0
        return 0;
    case RID_ROUTER_IP_ADDRESSES:
        anjay_dm_emit(ctx, 0);
        return 0;
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int resource_read(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                         anjay_iid_t iid, anjay_rid_t rid, anjay_riid_t riid,
                         anjay_output_ctx_t *ctx) {
    (void) iid;
    (void) riid;
    connectivity_ctx_t *obj = AVS_CONTAINER_OF(def, connectivity_ctx_t, def);

    // Live refresh for IP + gateway before serving those resources.
    if (anjay && (rid == RID_IP_ADDRESSES || rid == RID_ROUTER_IP_ADDRESSES)) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_ip_info_t info = (esp_netif_ip_info_t){ 0 };
            if (esp_netif_get_ip_info(netif, &info) == ESP_OK) {
                char new_ip[16] = "0.0.0.0";
                char new_gw[16] = "0.0.0.0";
                if (info.ip.addr) {
                    snprintf(new_ip, sizeof(new_ip), IPSTR, IP2STR(&info.ip));
                }
                if (info.gw.addr) {
                    snprintf(new_gw, sizeof(new_gw), IPSTR, IP2STR(&info.gw));
                }
                bool ip_changed = strcmp(new_ip, obj->ip_addr) != 0;
                bool gw_changed = strcmp(new_gw, obj->gw_addr) != 0;
                if (ip_changed) {
                    ESP_LOGI(TAG_CONN, "IP updated on read: %s -> %s", obj->ip_addr, new_ip);
                    strlcpy(obj->ip_addr, new_ip, sizeof(obj->ip_addr));
                    anjay_notify_changed(anjay, OID_CONNECTIVITY, 0, RID_IP_ADDRESSES);
                }
                if (gw_changed) {
                    ESP_LOGI(TAG_CONN, "GW updated on read: %s -> %s", obj->gw_addr, new_gw);
                    strlcpy(obj->gw_addr, new_gw, sizeof(obj->gw_addr));
                    anjay_notify_changed(anjay, OID_CONNECTIVITY, 0, RID_ROUTER_IP_ADDRESSES);
                }
            }
        }
    }

    switch (rid) {
    case RID_NETWORK_BEARER:
        // 41 = WLAN (per LwM2M Network Bearer registry)
        return anjay_ret_i32(ctx, 41);
    case RID_SIGNAL_STRENGTH:
        // Live refresh if possible
        if (update_from_wifi(obj)) {
            // notify link quality if changed by RSSI smoothing effect
            // (No notify here to avoid recursion; periodic update handles it)
        }
        return anjay_ret_i32(ctx, obj->signal_strength_dbm);
    case RID_LINK_QUALITY:
        if (update_from_wifi(obj)) {
            // recompute already done in update_from_wifi
        }
        return anjay_ret_i32(ctx, obj->link_quality_pct);
    case RID_IP_ADDRESSES:
        if (riid == ANJAY_ID_INVALID || riid == 0) {
            return anjay_ret_string(ctx, obj->ip_addr[0] ? obj->ip_addr : "0.0.0.0");
        }
        return ANJAY_ERR_NOT_FOUND;
    case RID_ROUTER_IP_ADDRESSES:
        if (riid == ANJAY_ID_INVALID || riid == 0) {
            return anjay_ret_string(ctx, obj->gw_addr[0] ? obj->gw_addr : "0.0.0.0");
        }
        return ANJAY_ERR_NOT_FOUND;
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static const anjay_dm_object_def_t OBJ_DEF = {
    .oid = OID_CONNECTIVITY,
    .version = "1.3",
    .handlers = {
        .list_instances = list_instances,
        .list_resources = list_resources,
        .list_resource_instances = list_resource_instances,
        .resource_read = resource_read,
    }
};

static connectivity_ctx_t g_ctx = {
    .def = &OBJ_DEF,
    .signal_strength_dbm = -60,
    .link_quality_pct = 85,
    .ip_addr = "0.0.0.0",
    .gw_addr = "0.0.0.0",
};

const anjay_dm_object_def_t *const *connectivity_object_def(void) {
    return &g_ctx.def;
}

void connectivity_object_update(anjay_t *anjay) {
    if (!anjay) {
        return;
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    char new_ip[16] = "0.0.0.0";
    char new_gw[16] = "0.0.0.0";
    if (netif) {
        esp_netif_ip_info_t info = (esp_netif_ip_info_t){ 0 };
        if (esp_netif_get_ip_info(netif, &info) == ESP_OK) {
            if (info.ip.addr) {
                snprintf(new_ip, sizeof(new_ip), IPSTR, IP2STR(&info.ip));
            }
            if (info.gw.addr) {
                snprintf(new_gw, sizeof(new_gw), IPSTR, IP2STR(&info.gw));
            }
        }
    } else {
        ESP_LOGW(TAG_CONN, "No WIFI_STA_DEF netif yet");
    }

    bool ip_changed = strcmp(new_ip, g_ctx.ip_addr) != 0;
    bool gw_changed = strcmp(new_gw, g_ctx.gw_addr) != 0;
    if (ip_changed) {
        ESP_LOGI(TAG_CONN, "Station IP changed: %s -> %s", g_ctx.ip_addr, new_ip);
        strlcpy(g_ctx.ip_addr, new_ip, sizeof(g_ctx.ip_addr));
        anjay_notify_changed(anjay, OID_CONNECTIVITY, 0, RID_IP_ADDRESSES);
    }
    if (gw_changed) {
        ESP_LOGI(TAG_CONN, "Gateway IP changed: %s -> %s", g_ctx.gw_addr, new_gw);
        strlcpy(g_ctx.gw_addr, new_gw, sizeof(g_ctx.gw_addr));
        anjay_notify_changed(anjay, OID_CONNECTIVITY, 0, RID_ROUTER_IP_ADDRESSES);
    }

    int32_t old_rssi = g_ctx.signal_strength_dbm;
    int32_t old_quality = g_ctx.link_quality_pct;
    if (!update_from_wifi(&g_ctx)) {
        // Decay values slowly if disconnected
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

