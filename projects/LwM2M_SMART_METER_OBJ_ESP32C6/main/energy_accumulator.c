#include "energy_accumulator.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <esp_log.h>
#include <cJSON.h>
#include <esp_vfs.h>

static const char *TAG_EACC = "EnergyAcc";

// Path inside SPIFFS or LittleFS; adjust to actual mounted FS path
#ifndef ENERGY_ACCUMULATOR_PATH
#define ENERGY_ACCUMULATOR_PATH "/spiffs/energy_acc.json"
#endif

#define PERSIST_INTERVAL_S 30.0

static void persist(const EnergyAccumulator *acc) {
    FILE *f = fopen(ENERGY_ACCUMULATOR_PATH, "w");
    if (!f) {
        ESP_LOGE(TAG_EACC, "persist fopen(%s) failed: %s", ENERGY_ACCUMULATOR_PATH, strerror(errno));
        return;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "kwh_import", acc->kwh_import);
    cJSON_AddNumberToObject(root, "kwh_export", acc->kwh_export);
    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        fprintf(f, "%s", json);
        cJSON_free(json);
    }
    cJSON_Delete(root);
    fclose(f);
    ESP_LOGI(TAG_EACC, "Persisted energy import=%.6f export=%.6f", acc->kwh_import, acc->kwh_export);
}

static void load(EnergyAccumulator *acc) {
    FILE *f = fopen(ENERGY_ACCUMULATOR_PATH, "r");
    if (!f) {
        ESP_LOGW(TAG_EACC, "No existing energy file %s (errno=%d)", ENERGY_ACCUMULATOR_PATH, errno);
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0 || sz > 4096) {
        fclose(f);
        ESP_LOGW(TAG_EACC, "Invalid energy file size %ld", sz);
        return;
    }
    rewind(f);
    char buf[4096];
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz) {
        ESP_LOGW(TAG_EACC, "Short read energy file");
        return;
    }
    buf[n] = '\0';
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGW(TAG_EACC, "JSON parse error for energy file");
        return;
    }
    cJSON *ji = cJSON_GetObjectItem(root, "kwh_import");
    cJSON *je = cJSON_GetObjectItem(root, "kwh_export");
    if (cJSON_IsNumber(ji)) acc->kwh_import = ji->valuedouble;
    if (cJSON_IsNumber(je)) acc->kwh_export = je->valuedouble;
    cJSON_Delete(root);
    ESP_LOGI(TAG_EACC, "Loaded energy import=%.6f export=%.6f", acc->kwh_import, acc->kwh_export);
}

void energy_acc_init(EnergyAccumulator *acc) {
    if (!acc) return;
    memset(acc, 0, sizeof(*acc));
    load(acc);
}

void energy_acc_add(EnergyAccumulator *acc, double power_kw, double dt_s, double now_s) {
    if (!acc || dt_s <= 0.0) return;
    double kwh = power_kw * dt_s / 3600.0; // power (kW) * hours -> kWh
    if (power_kw >= 0.0) acc->kwh_import += kwh; else acc->kwh_export += -kwh; // export stored positive
    if (acc->last_persist_time_s == 0.0) acc->last_persist_time_s = now_s;
    if ((now_s - acc->last_persist_time_s) >= PERSIST_INTERVAL_S) {
        persist(acc);
        acc->last_persist_time_s = now_s;
    }
}

void energy_acc_flush(EnergyAccumulator *acc, double now_s) {
    if (!acc) return;
    (void) now_s;
    persist(acc);
}
