#include "smart_meter_object.h"
#include <anjay/dm.h>
#include <anjay/io.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define OID_SMART_METER 10243
// Resource IDs per provided table
#define RID_MANUFACTURER                 0   // string
#define RID_MODEL_NUMBER                 1   // string
#define RID_SERIAL_NUMBER                2   // string
#define RID_DESCRIPTION                  3   // string
#define RID_TENSION                      4   // float (V)
#define RID_CURRENT                      5   // float (A) MANDATORY
#define RID_ACTIVE_POWER                 6   // float (kW)
#define RID_REACTIVE_POWER               7   // float (kvar)
#define RID_INDUCTIVE_REACTIVE_POWER     8   // float (kvar)
#define RID_CAPACITIVE_REACTIVE_POWER    9   // float (kvar)
#define RID_APPARENT_POWER               10  // float (kVA)
#define RID_POWER_FACTOR                 11  // float (-1..1)
#define RID_THD_V                        12  // float (/100)
#define RID_THD_A                        13  // float (/100)
#define RID_ACTIVE_ENERGY                14  // float (kWh)
#define RID_REACTIVE_ENERGY              15  // float (kvarh)
#define RID_APPARENT_ENERGY              16  // float (kVAh)
#define RID_FREQUENCY                    17  // float (Hz)

typedef struct {
    const anjay_dm_object_def_t *def;
    // Identification
    char manufacturer[32];
    char model_number[32];
    char serial_number[32];
    char description[64];

    // Measurements
    float current_a;      // A
    float active_power_kw;      // kW
    float reactive_power_kvar;  // kvar
    float inductive_reactive_power_kvar;  // kvar
    float capacitive_reactive_power_kvar; // kvar
    float apparent_power_kva;    // kVA
    float power_factor;          // -1..1
    float thd_v;                 // fraction (/100)
    float thd_a;                 // fraction (/100)
    float active_energy_kwh;     // kWh
    float reactive_energy_kvarh; // kvarh
    float apparent_energy_kvah;  // kVAh
    float frequency_hz;          // Hz

    // internal simulation state
    float voltage_v; // for internal calc (not exposed directly except as string)
    TickType_t last_update;
} sm_ctx_t;

static const char *TAG_SM = "sm_obj";

static sm_ctx_t g_sm;

static int list_instances(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                          anjay_dm_list_ctx_t *ctx) {
    (void) anjay; (void) def;
    anjay_dm_emit(ctx, 0);
    return 0;
}

static int list_resources(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                          anjay_iid_t iid, anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay; (void) def; (void) iid;
    ESP_LOGD(TAG_SM, "list_resources for /%d/%u", OID_SMART_METER, (unsigned) iid);
    // Identification
    anjay_dm_emit_res(ctx, RID_MANUFACTURER, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_MODEL_NUMBER, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_SERIAL_NUMBER, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_DESCRIPTION, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    // Measurements
    anjay_dm_emit_res(ctx, RID_TENSION, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_CURRENT, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_ACTIVE_POWER, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_REACTIVE_POWER, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_INDUCTIVE_REACTIVE_POWER, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_CAPACITIVE_REACTIVE_POWER, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_APPARENT_POWER, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_POWER_FACTOR, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_THD_V, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_THD_A, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_ACTIVE_ENERGY, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_REACTIVE_ENERGY, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_APPARENT_ENERGY, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_FREQUENCY, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    return 0;
}

static int resource_read(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                         anjay_iid_t iid, anjay_rid_t rid, anjay_riid_t riid,
                         anjay_output_ctx_t *ctx) {
    (void) anjay; (void) iid; (void) riid;
    sm_ctx_t *obj = AVS_CONTAINER_OF(def, sm_ctx_t, def);
    ESP_LOGD(TAG_SM, "read /%d/%u/%u", OID_SMART_METER, (unsigned) iid, (unsigned) rid);
    switch (rid) {
    // Identification
    case RID_MANUFACTURER:
        return anjay_ret_string(ctx, obj->manufacturer);
    case RID_MODEL_NUMBER:
        return anjay_ret_string(ctx, obj->model_number);
    case RID_SERIAL_NUMBER:
        return anjay_ret_string(ctx, obj->serial_number);
    case RID_DESCRIPTION:
        return anjay_ret_string(ctx, obj->description);

    // Measurements
    case RID_TENSION:
        return anjay_ret_float(ctx, obj->voltage_v);
    case RID_CURRENT:
        return anjay_ret_float(ctx, obj->current_a);
    case RID_ACTIVE_POWER:
        return anjay_ret_float(ctx, obj->active_power_kw);
    case RID_REACTIVE_POWER:
        return anjay_ret_float(ctx, obj->reactive_power_kvar);
    case RID_INDUCTIVE_REACTIVE_POWER:
        return anjay_ret_float(ctx, obj->inductive_reactive_power_kvar);
    case RID_CAPACITIVE_REACTIVE_POWER:
        return anjay_ret_float(ctx, obj->capacitive_reactive_power_kvar);
    case RID_APPARENT_POWER:
        return anjay_ret_float(ctx, obj->apparent_power_kva);
    case RID_POWER_FACTOR:
        return anjay_ret_float(ctx, obj->power_factor);
    case RID_THD_V:
        return anjay_ret_float(ctx, obj->thd_v);
    case RID_THD_A:
        return anjay_ret_float(ctx, obj->thd_a);
    case RID_ACTIVE_ENERGY:
        return anjay_ret_float(ctx, obj->active_energy_kwh);
    case RID_REACTIVE_ENERGY:
        return anjay_ret_float(ctx, obj->reactive_energy_kvarh);
    case RID_APPARENT_ENERGY:
        return anjay_ret_float(ctx, obj->apparent_energy_kvah);
    case RID_FREQUENCY:
        return anjay_ret_float(ctx, obj->frequency_hz);
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

// All resources are read-only according to the provided table; no write/exec handlers

static const anjay_dm_object_def_t OBJ_DEF = {
    .oid = OID_SMART_METER,
    .version = "2.0",
    .handlers = {
        .list_instances = list_instances,
        .list_resources = list_resources,
        .resource_read = resource_read,
        .resource_write = NULL,
        .resource_execute = NULL,
    }
};

const anjay_dm_object_def_t *const *smart_meter_object_create(void) {
    memset(&g_sm, 0, sizeof(g_sm));
    g_sm.def = &OBJ_DEF;
    // Identification defaults
    strncpy(g_sm.manufacturer, "ACME Power", sizeof(g_sm.manufacturer) - 1);
    strncpy(g_sm.model_number, "SPM-1PH", sizeof(g_sm.model_number) - 1);
    strncpy(g_sm.serial_number, "SN12345678", sizeof(g_sm.serial_number) - 1);
    strncpy(g_sm.description, "Single-phase smart meter", sizeof(g_sm.description) - 1);

    // Electrical defaults
    g_sm.voltage_v = 230.0f;
    g_sm.current_a = 0.50f;
    g_sm.power_factor = 0.90f;
    g_sm.frequency_hz = 60.0f;

    // Derived powers (kW/kvar/kVA)
    const float s_kva = (g_sm.voltage_v * g_sm.current_a) / 1000.0f;
    const float p_kw = s_kva * g_sm.power_factor;
    const float q_kvar = s_kva * sqrtf(fmaxf(0.0f, 1.0f - g_sm.power_factor * g_sm.power_factor));
    g_sm.apparent_power_kva = s_kva;
    g_sm.active_power_kw = p_kw;
    g_sm.reactive_power_kvar = q_kvar;
    g_sm.inductive_reactive_power_kvar = q_kvar; // assume inductive load
    g_sm.capacitive_reactive_power_kvar = 0.0f;

    // Energies start at 0
    g_sm.active_energy_kwh = 0.0f;
    g_sm.reactive_energy_kvarh = 0.0f;
    g_sm.apparent_energy_kvah = 0.0f;

    // THD (fractions)
    g_sm.thd_v = 0.02f; // 2%
    g_sm.thd_a = 0.03f; // 3%

    g_sm.last_update = xTaskGetTickCount();
    ESP_LOGI(TAG_SM, "Smart Meter(10243) created");
    return &g_sm.def;
}

void smart_meter_object_release(const anjay_dm_object_def_t *const *obj) {
    (void) obj; // static instance; nothing to free
}

void smart_meter_object_update(anjay_t *anjay, const anjay_dm_object_def_t *const *obj) {
    (void) obj;
    if (!anjay) { return; }
    const TickType_t now = xTaskGetTickCount();
    if ((now - g_sm.last_update) < pdMS_TO_TICKS(1000)) {
        return;
    }
    g_sm.last_update = now;

    float t = (float)(now % (120000)) / 1000.0f; // 2-minute cycle
    // Simulate small variations
    float new_freq = 60.0f + 0.05f * sinf(t * 0.2f);
    float new_voltage = 230.0f + 2.5f * sinf(t * 0.7f);
    float new_pf = 0.90f + 0.05f * sinf(t * 0.5f);
    new_pf = fminf(fmaxf(new_pf, -1.0f), 1.0f);
    float load_profile = 0.4f + 0.3f * (0.5f + 0.5f * sinf(t));
    float new_current = load_profile; // A

    // Apparent power (kVA), Active (kW), Reactive (kvar)
    const float s_kva = (new_voltage * new_current) / 1000.0f;
    const float p_kw = s_kva * new_pf;
    const float q_kvar_mag = s_kva * sqrtf(fmaxf(0.0f, 1.0f - new_pf * new_pf));
    // Assume inductive when sin component positive; simplistic split
    bool inductive = sinf(t * 0.3f) >= 0.0f;
    float new_q_kvar = q_kvar_mag * (inductive ? 1.0f : -1.0f);
    float new_q_ind_kvar = inductive ? q_kvar_mag : 0.0f;
    float new_q_cap_kvar = inductive ? 0.0f : q_kvar_mag;

    // Integrate energies (kWh, kvarh, kVAh)
    const float dt_hours = 1.0f / 3600.0f; // ~1 second
    float new_e_kwh = g_sm.active_energy_kwh + (p_kw * dt_hours);
    float new_e_kvarh = g_sm.reactive_energy_kvarh + (fabsf(new_q_kvar) * dt_hours);
    float new_e_kvah = g_sm.apparent_energy_kvah + (s_kva * dt_hours);

    // THD variations
    float new_thd_v = 0.02f + 0.005f * sinf(t * 0.11f);
    float new_thd_a = 0.03f + 0.008f * sinf(t * 0.09f + 0.5f);

    // Change detection
    bool ch_freq = fabsf(new_freq - g_sm.frequency_hz) > 0.001f;
    bool ch_volt = fabsf(new_voltage - g_sm.voltage_v) > 0.01f;
    bool ch_pf   = fabsf(new_pf - g_sm.power_factor) > 0.001f;
    bool ch_curr = fabsf(new_current - g_sm.current_a) > 0.001f;
    bool ch_pkw  = fabsf(p_kw - g_sm.active_power_kw) > 0.001f;
    bool ch_q    = fabsf(new_q_kvar - g_sm.reactive_power_kvar) > 0.001f;
    bool ch_qi   = fabsf(new_q_ind_kvar - g_sm.inductive_reactive_power_kvar) > 0.001f;
    bool ch_qc   = fabsf(new_q_cap_kvar - g_sm.capacitive_reactive_power_kvar) > 0.001f;
    bool ch_s    = fabsf(s_kva - g_sm.apparent_power_kva) > 0.001f;
    bool ch_e    = fabsf(new_e_kwh - g_sm.active_energy_kwh) > 0.0001f;
    bool ch_eq   = fabsf(new_e_kvarh - g_sm.reactive_energy_kvarh) > 0.0001f;
    bool ch_es   = fabsf(new_e_kvah - g_sm.apparent_energy_kvah) > 0.0001f;
    bool ch_thdv = fabsf(new_thd_v - g_sm.thd_v) > 0.0001f;
    bool ch_thda = fabsf(new_thd_a - g_sm.thd_a) > 0.0001f;

    // Apply
    g_sm.frequency_hz = new_freq;
    g_sm.voltage_v = new_voltage;
    g_sm.power_factor = new_pf;
    g_sm.current_a = new_current;
    g_sm.active_power_kw = p_kw;
    g_sm.reactive_power_kvar = new_q_kvar;
    g_sm.inductive_reactive_power_kvar = new_q_ind_kvar;
    g_sm.capacitive_reactive_power_kvar = new_q_cap_kvar;
    g_sm.apparent_power_kva = s_kva;
    g_sm.active_energy_kwh = new_e_kwh;
    g_sm.reactive_energy_kvarh = new_e_kvarh;
    g_sm.apparent_energy_kvah = new_e_kvah;
    g_sm.thd_v = new_thd_v;
    g_sm.thd_a = new_thd_a;

    // Notifications
    if (ch_volt) anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_TENSION);
    if (ch_curr) anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_CURRENT);
    if (ch_pkw)  anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_ACTIVE_POWER);
    if (ch_q)    anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_REACTIVE_POWER);
    if (ch_qi)   anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_INDUCTIVE_REACTIVE_POWER);
    if (ch_qc)   anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_CAPACITIVE_REACTIVE_POWER);
    if (ch_s)    anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_APPARENT_POWER);
    if (ch_pf)   anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_POWER_FACTOR);
    if (ch_thdv) anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_THD_V);
    if (ch_thda) anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_THD_A);
    if (ch_e)    anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_ACTIVE_ENERGY);
    if (ch_eq)   anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_REACTIVE_ENERGY);
    if (ch_es)   anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_APPARENT_ENERGY);
    if (ch_freq) anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_FREQUENCY);
}
