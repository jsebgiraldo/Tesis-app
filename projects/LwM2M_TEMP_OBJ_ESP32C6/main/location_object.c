#include "location_object.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <time.h>
#include <sys/time.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <anjay/io.h>
#include "sdkconfig.h"
#if CONFIG_GEOLOC_ENABLE
#include <esp_http_client.h>
#include <cJSON.h>
#endif

#define OID_LOCATION 6
#define RID_LATITUDE 0
#define RID_LONGITUDE 1
#define RID_ALTITUDE 2
#define RID_TIMESTAMP 5

static const char *TAG_LOC = "loc_obj";

// --------------------------------------------------------------------------
// Fallbacks for newly added Kconfig symbols (build will still succeed even if
// sdkconfig hasn't been regenerated yet). Once you run `idf.py menuconfig`
// and save, these will come from sdkconfig.h instead.
// --------------------------------------------------------------------------
#ifndef CONFIG_GEOLOC_FALLBACK_LAT
#define CONFIG_GEOLOC_FALLBACK_LAT "5.000000"
#endif
#ifndef CONFIG_GEOLOC_FALLBACK_LON
#define CONFIG_GEOLOC_FALLBACK_LON "-74.000000"
#endif
#ifndef CONFIG_GEOLOC_REFRESH_MINUTES
#define CONFIG_GEOLOC_REFRESH_MINUTES 60
#endif
#ifndef CONFIG_GEOLOC_HTTP_TIMEOUT_MS
#define CONFIG_GEOLOC_HTTP_TIMEOUT_MS 2500
#endif
// If persistence option not yet in sdkconfig, assume enabled to keep behavior
#ifndef CONFIG_GEOLOC_PERSIST_NVS
#define CONFIG_GEOLOC_PERSIST_NVS 1
#endif

typedef struct {
    const anjay_dm_object_def_t *def;
    float latitude;   // degrees
    float longitude;  // degrees
    int64_t timestamp; // seconds since epoch
#if CONFIG_GEOLOC_ENABLE
    TickType_t next_refresh_ticks;
    bool loaded_from_nvs;
#endif
} location_ctx_t;

static bool s_time_synced = false;

static void time_sync_notification_cb(struct timeval *tv) {
    (void) tv;
    s_time_synced = true;
    ESP_LOGI(TAG_LOC, "SNTP time synchronized");
}

static void init_sntp_if_needed(void) {
    if (esp_sntp_enabled()) {
        return;
    }
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    // Use pool servers or region-specific; pool.ntp.org is fine here
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
    ESP_LOGI(TAG_LOC, "SNTP init started");
}

static int64_t platform_time_seconds(void) {
    if (!s_time_synced) {
        return (int64_t) (xTaskGetTickCount() / configTICK_RATE_HZ);
    }
    struct timeval now;
    if (gettimeofday(&now, NULL) == 0) {
        return (int64_t) now.tv_sec;
    }
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
    // Emit in ascending RID order: 0,1,2,5
    anjay_dm_emit_res(ctx, RID_LATITUDE,  ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_LONGITUDE, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_ALTITUDE,  ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
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
        double v = obj->latitude;
        if (v > 90.0) v = 90.0; else if (v < -90.0) v = -90.0;
        ESP_LOGD(TAG_LOC, "read Latitude(float) -> %f", v);
        return anjay_ret_double(ctx, v);
    }
    case RID_LONGITUDE: {
        double v = obj->longitude;
        if (v > 180.0) v = 180.0; else if (v < -180.0) v = -180.0;
        ESP_LOGD(TAG_LOC, "read Longitude(float) -> %f", v);
        return anjay_ret_double(ctx, v);
    }
    case RID_ALTITUDE: {
        double alt = 0.0; // Placeholder altitude in meters
        ESP_LOGD(TAG_LOC, "read Altitude(float) -> %f", alt);
        return anjay_ret_double(ctx, alt);
    }
    case RID_TIMESTAMP: {
        // Always refresh the timestamp right before returning if SNTP synced
        if (s_time_synced) {
            ((location_ctx_t*)obj)->timestamp = platform_time_seconds();
        }
        ESP_LOGD(TAG_LOC, "read Timestamp -> %lld", (long long) obj->timestamp);
        return anjay_ret_i64(ctx, obj->timestamp);
    }
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
    // Initialized to 0; will apply fallback or persisted values at runtime
    .latitude = 0.0f,
    .longitude = 0.0f,
    .timestamp = 0
};

const anjay_dm_object_def_t **location_object_create(void) {
    // Initialize SNTP (time zone: America/Bogota)
    setenv("TZ", "America/Bogota", 1); // TZ database name; ESP-IDF uses newlib which supports zoneinfo if configured
    tzset();
    init_sntp_if_needed();
    // Optionally wait a short grace period for first sync (non-blocking loop with timeout)
    const TickType_t start = xTaskGetTickCount();
    const TickType_t wait_ticks = pdMS_TO_TICKS(3000); // up to 3s
    while (!s_time_synced && (xTaskGetTickCount() - start) < wait_ticks) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    // Apply fallback coordinates first; they may later be overridden by NVS or GeoIP
    char *endp = NULL;
    double fb_lat = strtod(CONFIG_GEOLOC_FALLBACK_LAT, &endp);
    if (endp && *endp == '\0') {
        g_loc.latitude = (float) fb_lat;
    }
    endp = NULL;
    double fb_lon = strtod(CONFIG_GEOLOC_FALLBACK_LON, &endp);
    if (endp && *endp == '\0') {
        g_loc.longitude = (float) fb_lon;
    }
    g_loc.timestamp = platform_time_seconds();
    ESP_LOGI(TAG_LOC, "Location(6) created (fallback) lat=%.6f lon=%.6f ts=%lld (time_synced=%d)", (double) g_loc.latitude, (double) g_loc.longitude, (long long) g_loc.timestamp, (int) s_time_synced);
#if CONFIG_GEOLOC_ENABLE
    g_loc.next_refresh_ticks = xTaskGetTickCount();
    g_loc.loaded_from_nvs = false;
#if CONFIG_GEOLOC_PERSIST_NVS
    // Attempt to load last stored lat/lon from NVS
    nvs_handle_t h;
    if (nvs_open("loc", NVS_READONLY, &h) == ESP_OK) {
        float lat = 0, lon = 0; size_t sz = sizeof(float);
        if (nvs_get_blob(h, "lat", &lat, &sz) == ESP_OK && sz == sizeof(float)) {
            sz = sizeof(float);
            if (nvs_get_blob(h, "lon", &lon, &sz) == ESP_OK && sz == sizeof(float)) {
                g_loc.latitude = lat;
                g_loc.longitude = lon;
                g_loc.loaded_from_nvs = true;
                ESP_LOGI(TAG_LOC, "Loaded persisted location lat=%.6f lon=%.6f", (double)lat, (double)lon);
            }
        }
        nvs_close(h);
    }
#endif
#endif
    return &g_loc.def;
}

void location_object_release(const anjay_dm_object_def_t **def) {
    (void) def;
}

void location_object_update(anjay_t *anjay, const anjay_dm_object_def_t **def) {
    (void) def;
    if (!anjay) { return; }
#if CONFIG_GEOLOC_ENABLE
    // Refresh via GeoIP per configured interval
    const TickType_t now = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(CONFIG_GEOLOC_REFRESH_MINUTES * 60 * 1000);
    if ((int32_t)(now - g_loc.next_refresh_ticks) >= 0) {
        if (!s_time_synced) {
            // Defer geolocation until we have real time (optional policy)
            g_loc.next_refresh_ticks = now + pdMS_TO_TICKS(30 * 1000); // retry in 30s
            return;
        }
        float new_lat = g_loc.latitude;
        float new_lon = g_loc.longitude;
        bool got = false;

        // Perform a simple HTTP GET to public GeoIP service and parse JSON
        esp_http_client_config_t http_cfg = {
#if CONFIG_GEOLOC_USE_SERVER_HOST
            .url = CONFIG_GEOLOC_BASE_URL,
#else
            .url = "http://ip-api.com/json",
#endif
            .timeout_ms = CONFIG_GEOLOC_HTTP_TIMEOUT_MS,
        };
        esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
        if (client) {
            if (esp_http_client_open(client, 0) == ESP_OK) {
                int status = 0;
                (void) esp_http_client_fetch_headers(client); // may return -1 if unknown/chunked
                char *buf = NULL;
                size_t cap = 2048; // enough for typical GeoIP JSON
                size_t len = 0;
                buf = (char *) malloc(cap);
                if (buf) {
                    while (1) {
                        if (len == cap) {
                            // Stop to avoid OOM; we only need first couple KB
                            break;
                        }
                        int to_read = (int) (cap - len);
                        int r = esp_http_client_read(client, buf + len, to_read);
                        if (r <= 0) {
                            break;
                        }
                        len += (size_t) r;
                    }
                    // finalize and parse if non-empty and HTTP 200
                    status = esp_http_client_get_status_code(client);
                    if (len > 0 && status == 200) {
                        if (len == cap) {
                            // ensure space for terminator
                            len -= 1;
                        }
                        buf[len] = '\0';
                        cJSON *root = cJSON_Parse(buf);
                        if (root) {
                            // ip-api.com returns {"lat": <double>, "lon": <double>, ...}
                            const cJSON *jlat = cJSON_GetObjectItemCaseSensitive(root, "lat");
                            const cJSON *jlon = cJSON_GetObjectItemCaseSensitive(root, "lon");
                            if (cJSON_IsNumber(jlat) && cJSON_IsNumber(jlon)) {
                                new_lat = (float) jlat->valuedouble;
                                new_lon = (float) jlon->valuedouble;
                                got = true;
                            } else {
                                // ipinfo.io returns {"loc":"lat,lon", ...}
                                const cJSON *jloc = cJSON_GetObjectItemCaseSensitive(root, "loc");
                                if (cJSON_IsString(jloc) && jloc->valuestring) {
                                    double dlat = 0, dlon = 0;
                                    if (sscanf(jloc->valuestring, "%lf,%lf", &dlat, &dlon) == 2) {
                                        new_lat = (float) dlat; new_lon = (float) dlon; got = true;
                                    }
                                }
                            }
                            cJSON_Delete(root);
                        }
                    }
                    free(buf);
                }
                (void) esp_http_client_close(client);
            }
            esp_http_client_cleanup(client);
        }

    int64_t new_ts = platform_time_seconds();
        bool lat_changed = got && fabsf(new_lat - g_loc.latitude) > 1e-6f;
        bool lon_changed = got && fabsf(new_lon - g_loc.longitude) > 1e-6f;
        bool ts_changed = (new_ts != g_loc.timestamp);
        if (got) {
            g_loc.latitude = new_lat;
            g_loc.longitude = new_lon;
#if CONFIG_GEOLOC_PERSIST_NVS
            // Persist new coordinates
            nvs_handle_t h;
            if (nvs_open("loc", NVS_READWRITE, &h) == ESP_OK) {
                (void)nvs_set_blob(h, "lat", &g_loc.latitude, sizeof(float));
                (void)nvs_set_blob(h, "lon", &g_loc.longitude, sizeof(float));
                (void)nvs_commit(h);
                nvs_close(h);
            }
#endif
        }
        g_loc.timestamp = new_ts;

        if (lat_changed) { anjay_notify_changed(anjay, OID_LOCATION, 0, RID_LATITUDE); }
        if (lon_changed) { anjay_notify_changed(anjay, OID_LOCATION, 0, RID_LONGITUDE); }
        if (ts_changed) { anjay_notify_changed(anjay, OID_LOCATION, 0, RID_TIMESTAMP); }

        g_loc.next_refresh_ticks = now + period;
    }
#else
    // Fallback demo: slight movement over time
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

    if (lat_changed) { anjay_notify_changed(anjay, OID_LOCATION, 0, RID_LATITUDE); }
    if (lon_changed) { anjay_notify_changed(anjay, OID_LOCATION, 0, RID_LONGITUDE); }
    if (ts_changed) { anjay_notify_changed(anjay, OID_LOCATION, 0, RID_TIMESTAMP); }
#endif
}
