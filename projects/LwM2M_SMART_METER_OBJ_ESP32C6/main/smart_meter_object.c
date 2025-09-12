#include "smart_meter_object.h"
#include <math.h>
#include <stdbool.h>
#include <anjay/anjay.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Simple mocked single-phase power meter source
static void read_instantaneous(float *voltage_v, float *current_a, float *pf, float *freq_hz) {
    extern unsigned long xTaskGetTickCount(void);
    float t = (float) (xTaskGetTickCount() % 100000) / 1000.0f;
    float v = 230.0f + 5.0f * sinf(t * 0.2f);
    float i = 1.50f + 0.30f * sinf(t * 0.5f + 1.0f);
    float p = 0.92f + 0.05f * sinf(t * 0.1f + 2.0f);
    if (p > 1.0f) p = 1.0f;
    if (p < -1.0f) p = -1.0f;
    float f = 60.0f + 0.05f * sinf(t * 0.3f);
    if (voltage_v) *voltage_v = v;
    if (current_a) *current_a = i;
    if (pf) *pf = p;
    if (freq_hz) *freq_hz = f;
}

static float apparent_power_kva(float v, float i) {
    return (v * i) / 1000.0f;
}

static float active_power_kw(float v, float i, float pf) {
    return (v * i * pf) / 1000.0f;
}

static float reactive_power_kvar(float v, float i, float pf) {
    float pf2 = pf * pf;
    if (pf2 > 1.0f) pf2 = 1.0f;
    float sinphi = sqrtf(fmaxf(0.0f, 1.0f - pf2));
    return (v * i * sinphi) / 1000.0f;
}

// Simple energy accumulators
static double g_e_active_kwh = 0.0;
static double g_e_reactive_kvarh = 0.0;
static double g_e_apparent_kvah = 0.0;
static TickType_t g_last_tick = 0;

static void update_energy_accumulators(void) {
    TickType_t now = xTaskGetTickCount();
    if (g_last_tick == 0) {
        g_last_tick = now;
        return;
    }
    TickType_t dt_ticks = now - g_last_tick;
    g_last_tick = now;
    float dt_h = (float) dt_ticks / (float) configTICK_RATE_HZ / 3600.0f;
    float v,i,pf,f;
    read_instantaneous(&v, &i, &pf, &f);
    (void) f;
    float p_kw = active_power_kw(v, i, pf);
    float q_kvar = reactive_power_kvar(v, i, pf);
    float s_kva = apparent_power_kva(v, i);
    g_e_active_kwh += p_kw * dt_h;
    g_e_reactive_kvarh += q_kvar * dt_h;
    g_e_apparent_kvah += s_kva * dt_h;
}

static int sm_list_instances(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                             anjay_dm_list_ctx_t *ctx) {
    (void) anjay; (void) def;
    anjay_dm_emit(ctx, 0);
    return 0;
}

static int sm_list_resources(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                             anjay_iid_t iid, anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay; (void) def; (void) iid;
    // Identification
    anjay_dm_emit_res(ctx, 0, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT); // Manufacturer
    anjay_dm_emit_res(ctx, 1, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT); // Model Number
    anjay_dm_emit_res(ctx, 2, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT); // Serial Number
    anjay_dm_emit_res(ctx, 3, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT); // Description
    // Electrical measurements
    anjay_dm_emit_res(ctx, 4, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT); // Tension (Voltage)
    anjay_dm_emit_res(ctx, 5, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT); // Current
    anjay_dm_emit_res(ctx, 6, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT); // Active Power
    anjay_dm_emit_res(ctx, 7, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT); // Reactive Power
    anjay_dm_emit_res(ctx, 8, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT); // Inductive Reactive Power
    anjay_dm_emit_res(ctx, 9, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT); // Capacitive Reactive Power
    anjay_dm_emit_res(ctx, 10, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT); // Apparent Power
    anjay_dm_emit_res(ctx, 11, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT); // Power Factor
    anjay_dm_emit_res(ctx, 12, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT); // THD-V
    anjay_dm_emit_res(ctx, 13, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT); // THD-A
    anjay_dm_emit_res(ctx, 14, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT); // Active Energy
    anjay_dm_emit_res(ctx, 15, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT); // Reactive Energy
    anjay_dm_emit_res(ctx, 16, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT); // Apparent Energy
    anjay_dm_emit_res(ctx, 17, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT); // Frequency
    return 0;
}

static int sm_read(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                   anjay_iid_t iid, anjay_rid_t rid, anjay_riid_t riid,
                   anjay_output_ctx_t *out_ctx) {
    (void) anjay; (void) def; (void) iid; (void) riid;
    switch (rid) {
    case 0: return anjay_ret_string(out_ctx, "Acme Power");
    case 1: return anjay_ret_string(out_ctx, "ESP32C6-Meter");
    case 2: return anjay_ret_string(out_ctx, "SN-0001");
    case 3: return anjay_ret_string(out_ctx, "Single-Phase Power Meter");
    case 4: { // Voltage (V)
        float v; read_instantaneous(&v, NULL, NULL, NULL);
        return anjay_ret_float(out_ctx, v);
    }
    case 5: { // Current (A)
        float i; read_instantaneous(NULL, &i, NULL, NULL);
        return anjay_ret_float(out_ctx, i);
    }
    case 6: { // Active Power (kW)
        float v,i,pf; read_instantaneous(&v, &i, &pf, NULL);
        return anjay_ret_float(out_ctx, active_power_kw(v, i, pf));
    }
    case 7: { // Reactive Power (kvar)
        float v,i,pf; read_instantaneous(&v, &i, &pf, NULL);
        return anjay_ret_float(out_ctx, reactive_power_kvar(v, i, pf));
    }
    case 8: { // Inductive Reactive Power (kvar)
        float v,i,pf; read_instantaneous(&v, &i, &pf, NULL);
        float q = reactive_power_kvar(v, i, pf);
        return anjay_ret_float(out_ctx, q);
    }
    case 9: { // Capacitive Reactive Power (kvar)
        return anjay_ret_float(out_ctx, 0.0f);
    }
    case 10: { // Apparent Power (kVA)
        float v,i; read_instantaneous(&v, &i, NULL, NULL);
        return anjay_ret_float(out_ctx, apparent_power_kva(v, i));
    }
    case 11: { // Power Factor (-1..1)
        float pf; read_instantaneous(NULL, NULL, &pf, NULL);
        return anjay_ret_float(out_ctx, pf);
    }
    case 12: { // THD-V (/100)
        return anjay_ret_float(out_ctx, 0.03f);
    }
    case 13: { // THD-A (/100)
        return anjay_ret_float(out_ctx, 0.05f);
    }
    case 14: { // Active Energy (kWh)
        update_energy_accumulators();
        return anjay_ret_double(out_ctx, g_e_active_kwh);
    }
    case 15: { // Reactive Energy (kvarh)
        update_energy_accumulators();
        return anjay_ret_double(out_ctx, g_e_reactive_kvarh);
    }
    case 16: { // Apparent Energy (kVAh)
        update_energy_accumulators();
        return anjay_ret_double(out_ctx, g_e_apparent_kvah);
    }
    case 17: { // Frequency (Hz)
        float f; read_instantaneous(NULL, NULL, NULL, &f);
        return anjay_ret_float(out_ctx, f);
    }
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static const anjay_dm_object_def_t OBJ10243 = {
    .oid = 10243,
    .handlers = {
        .list_instances = sm_list_instances,
        .list_resources = sm_list_resources,
        .resource_read = sm_read,
    }
};

static const anjay_dm_object_def_t *const OBJ10243_DEF = &OBJ10243;

const anjay_dm_object_def_t *const *smart_meter_object_def(void) {
    return &OBJ10243_DEF;
}

