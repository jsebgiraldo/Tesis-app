#include "location_object.h"
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <esp_log.h>
#include <esp_http_client.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <anjay/io.h>
// SNTP removed: no time sync initialization here. We rely on system time if valid, otherwise monotonic ticks.

#define OID_LOCATION 6
#define RID_LATITUDE 0
#define RID_LONGITUDE 1
#define RID_TIMESTAMP 5

static const char *TAG_LOC = "loc_obj";

typedef struct {
    const anjay_dm_object_def_t *def;
    float latitude;   // degrees
    float longitude;  // degrees
    int64_t timestamp; // seconds since epoch
} location_ctx_t;
#ifdef CONFIG_GEOLOC_ENABLE
// Optional: try to get approximate lat/lon from a GeoIP service
static void geolocate_initial(float *lat_out, float *lon_out) {
    if (!lat_out || !lon_out) { return; }
    char url[192] = {0};
#if CONFIG_GEOLOC_USE_SERVER_HOST
    // get server resolved host from Kconfig
#if CONFIG_LWM2M_OVERRIDE_HOSTNAME_ENABLE
    const char *host = CONFIG_LWM2M_OVERRIDE_HOSTNAME;
#else
    const char *host = "";
#endif
    if (host && host[0]) {
        snprintf(url, sizeof(url), "%s/%s", CONFIG_GEOLOC_BASE_URL, host);
    } else {
        snprintf(url, sizeof(url), "%s", CONFIG_GEOLOC_BASE_URL);
    }
#else
    snprintf(url, sizeof(url), "%s", CONFIG_GEOLOC_BASE_URL);
#endif

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 4000,
        .method = HTTP_METHOD_GET,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { return; }
    if (esp_http_client_open(client, 0) == ESP_OK) {
        // Fetch headers (optional) then read response into a local buffer
        (void) esp_http_client_fetch_headers(client);
        char buf[2048];
        int r = esp_http_client_read_response(client, buf, sizeof(buf) - 1);
        if (r >= 0) {
            buf[r] = '\0';
            int status = esp_http_client_get_status_code(client);
            if (status >= 200 && status < 300) {
                cJSON *root = cJSON_Parse(buf);
                if (root) {
                    const cJSON *lat = cJSON_GetObjectItemCaseSensitive(root, "lat");
                    const cJSON *lon = cJSON_GetObjectItemCaseSensitive(root, "lon");
                    if (cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
                        *lat_out = (float) lat->valuedouble;
                        *lon_out = (float) lon->valuedouble;
                        ESP_LOGI(TAG_LOC, "GeoIP: lat=%.6f lon=%.6f", (double) *lat_out, (double) *lon_out);
                    } else {
                        ESP_LOGW(TAG_LOC, "GeoIP JSON missing lat/lon");
                    }
                    cJSON_Delete(root);
                } else {
                    ESP_LOGW(TAG_LOC, "GeoIP: JSON parse failed");
                }
            } else {
                ESP_LOGW(TAG_LOC, "GeoIP HTTP status: %d", status);
            }
        } else {
            ESP_LOGW(TAG_LOC, "GeoIP: read_response failed (%d)", r);
        }
        esp_http_client_close(client);
    }
    esp_http_client_cleanup(client);
}
#endif

// --- Time handling: prefer real epoch if available, fallback to monotonic ticks ---
static inline int64_t platform_time_seconds(void) {
    time_t now = 0;
    time(&now);
    const time_t MIN_VALID_EPOCH = (time_t) 1609459200; // 2021-01-01
    if (now >= MIN_VALID_EPOCH) {
        return (int64_t) now;
    }
    // Fallback: monotonic seconds from FreeRTOS tick (not real epoch)
    return (int64_t) (xTaskGetTickCount() / configTICK_RATE_HZ);
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
    anjay_dm_emit_res(ctx, RID_LATITUDE, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_LONGITUDE, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_TIMESTAMP, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    return 0;
}

static int resource_read(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                anjay_iid_t iid, anjay_rid_t rid, anjay_riid_t riid,
                anjay_output_ctx_t *ctx) {
    (void) anjay; (void) iid; (void) riid;
    const location_ctx_t *obj = AVS_CONTAINER_OF(def, location_ctx_t, def);
    switch (rid) {
    case RID_LATITUDE: {
        char buf[32];
        // Use enough precision for degrees
        int n = snprintf(buf, sizeof(buf), "%.6f", (double) obj->latitude);
        (void) n;
        return anjay_ret_string(ctx, buf);
    }
    case RID_LONGITUDE: {
        char buf[32];
        int n = snprintf(buf, sizeof(buf), "%.6f", (double) obj->longitude);
        (void) n;
        return anjay_ret_string(ctx, buf);
    }
    case RID_TIMESTAMP:
        return anjay_ret_i64(ctx, obj->timestamp);
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static const anjay_dm_object_def_t OBJ_DEF = {
    .oid = OID_LOCATION,
    .version = "1.0",
    .handlers = {
        .list_instances = list_instances,
        .list_resources = list_resources,
        .resource_read = resource_read,
        // defaults for unsupported handlers are NULL
    }
};

static location_ctx_t g_loc = {
    .def = &OBJ_DEF,
    .latitude = 20.0f,
    .longitude = 140.0f,
    .timestamp = 0
};

const anjay_dm_object_def_t **location_object_create(void) {
    g_loc.timestamp = platform_time_seconds();
#ifdef CONFIG_GEOLOC_ENABLE
    float lat = g_loc.latitude;
    float lon = g_loc.longitude;
    geolocate_initial(&lat, &lon);
    g_loc.latitude = lat;
    g_loc.longitude = lon;
#endif
    ESP_LOGI(TAG_LOC, "Location(6) created: lat=%.6f lon=%.6f ts=%lld", (double) g_loc.latitude, (double) g_loc.longitude, (long long) g_loc.timestamp);
    return &g_loc.def;
}

void location_object_release(const anjay_dm_object_def_t **def) {
    (void) def;
}

void location_object_update(anjay_t *anjay, const anjay_dm_object_def_t **def) {
    (void) def;
    if (!anjay) { return; }
    // Simulate slight movement over time and update timestamp
    TickType_t ticks = xTaskGetTickCount();
    float t = (float) (ticks % 100000) / 1000.0f;
    float new_lat = 20.0f + 0.001f * sinf(t);
    float new_lon = 140.0f + 0.001f * cosf(t);
    int64_t new_ts = platform_time_seconds();

    bool lat_changed = fabsf(new_lat - g_loc.latitude) > 1e-6f;
    bool lon_changed = fabsf(new_lon - g_loc.longitude) > 1e-6f;
    bool ts_changed = (new_ts != g_loc.timestamp);

    g_loc.latitude = new_lat;
    g_loc.longitude = new_lon;
    g_loc.timestamp = new_ts;

    if (lat_changed) {
        anjay_notify_changed(anjay, OID_LOCATION, 0, RID_LATITUDE);
    }
    if (lon_changed) {
        anjay_notify_changed(anjay, OID_LOCATION, 0, RID_LONGITUDE);
    }
    if (ts_changed) {
        anjay_notify_changed(anjay, OID_LOCATION, 0, RID_TIMESTAMP);
    }
}
