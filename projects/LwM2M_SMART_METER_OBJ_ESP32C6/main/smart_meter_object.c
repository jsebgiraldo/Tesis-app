#include "smart_meter_object.h"
#include <anjay/dm.h>
#include <anjay/io.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_random.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <anjay/attr_storage.h>
#include "sdkconfig.h"
#include "power_model.h"
#include "energy_accumulator.h"

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
// New RW resources for simulation control
#define RID_SIM_MODE                     60000 // int: 0=periodic,1=dynamic
#define RID_UPDATE_PERIOD                60001 // int: seconds (1..3600)

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
    // runtime control
    bool dynamic_mode; // false=periodic (use stored values), true=per-read dynamic
    uint32_t update_period_sec; // integration/notification period
    TickType_t last_dyn_notify; // last fast notify tick (dynamic mode)
    bool attrs_initialized; // if initial pmin/pmax sync done
    // Delta notification state
    bool first_notify_done;
    float ln_voltage_v;
    float ln_current_a;
    float ln_active_power_kw;
    float ln_reactive_power_kvar;
    float ln_inductive_reactive_power_kvar;
    float ln_capacitive_reactive_power_kvar;
    float ln_apparent_power_kva;
    float ln_power_factor;
    float ln_thd_v;
    float ln_thd_a;
    float ln_frequency_hz;
    float ln_active_energy_kwh;
    float ln_reactive_energy_kvarh;
    float ln_apparent_energy_kvah;
    // new: model tick for continuous instantaneous evolution
    TickType_t last_model_tick;
    // energy accumulator (import/export separation)
    EnergyAccumulator energy_acc;
} sm_ctx_t;

static const char *TAG_SM = "sm_obj";

static sm_ctx_t g_sm;

// Delta thresholds for notifications
#define SM_DELTA_VOLTAGE      0.10f
#define SM_DELTA_CURRENT      0.01f
#define SM_DELTA_POWER        0.002f
#define SM_DELTA_PF           0.005f
#define SM_DELTA_THD          0.005f
#define SM_DELTA_FREQ         0.01f
#define SM_DELTA_ENERGY       0.0005f

// Forward decl for attribute sync
static void sm_sync_attrs(anjay_t *anjay);

// Attribute synchronization: set pmin/pmax depending on mode
// Strategy: in dynamic_mode -> pmin=1 (fast), pmax=update_period_sec (never exceed main integration period)
//           in periodic mode -> pmin=update_period_sec, pmax=update_period_sec*2 (coarse notifications)
// Applied to key instantaneous resources to control traffic.
static void sm_sync_attrs(anjay_t *anjay) {
#ifdef ANJAY_WITH_ATTR_STORAGE
    if (!anjay) { return; }
    const bool dyn = g_sm.dynamic_mode;
    uint32_t up = g_sm.update_period_sec ? g_sm.update_period_sec : 1;
    int32_t pmin_dyn = 1;
    int32_t pmax_dyn = (int32_t) up;
    int32_t pmin_per = (int32_t) up;
    int32_t pmax_per = (int32_t) (up * 2);
    const anjay_rid_t inst_rids[] = { RID_TENSION, RID_CURRENT, RID_ACTIVE_POWER, RID_REACTIVE_POWER, RID_APPARENT_POWER, RID_POWER_FACTOR, RID_THD_V, RID_THD_A, RID_FREQUENCY };
    anjay_dm_r_attributes_t attrs = ANJAY_DM_R_ATTRIBUTES_EMPTY;
    attrs.common.min_period = dyn ? pmin_dyn : pmin_per;
    attrs.common.max_period = dyn ? pmax_dyn : pmax_per;
    for (size_t i = 0; i < sizeof(inst_rids)/sizeof(inst_rids[0]); ++i) {
#ifdef CONFIG_LWM2M_SERVER_SHORT_ID
        (void) anjay_attr_storage_set_resource_attrs(anjay, (anjay_ssid_t) CONFIG_LWM2M_SERVER_SHORT_ID, OID_SMART_METER, 0, inst_rids[i], &attrs);
#else
        (void) anjay_attr_storage_set_resource_attrs(anjay, (anjay_ssid_t) 123, OID_SMART_METER, 0, inst_rids[i], &attrs);
#endif
    }
    ESP_LOGI(TAG_SM, "Synced attrs (%s): dyn(pmin=%d pmax=%d) periodic(pmin=%d pmax=%d)", dyn?"dynamic":"periodic", (int) pmin_dyn, (int) pmax_dyn, (int) pmin_per, (int) pmax_per);
    g_sm.attrs_initialized = true;
#else
    (void) anjay;
#endif
}

// --- Random helpers ---------------------------------------------------------
static inline float frand_unit(void) {
    // Uniform in [0,1)
    return (float) ((double) esp_random() / (double) UINT32_MAX);
}

static inline float frand_range(float a, float b) {
    return a + (b - a) * frand_unit();
}

static inline float clampf(float x, float lo, float hi) {
    return fminf(hi, fmaxf(lo, x));
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
    // Control resources
    anjay_dm_emit_res(ctx, RID_SIM_MODE, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_UPDATE_PERIOD, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
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
    case RID_TENSION: {
        // Per-read jitter: ±0.25 V around current stored voltage, clamp to realistic limits
        float jitter = frand_range(-0.25f, 0.25f);
        float v_out = clampf(obj->voltage_v + jitter, 205.0f, 255.0f);
        return anjay_ret_float(ctx, v_out);
    }
    case RID_CURRENT: {
        // Per-read jitter: ±0.04 A scaled by load fraction, ensures visible change but realistic
        float load_frac = clampf(obj->current_a / 6.0f, 0.0f, 1.2f);
        float mag = 0.04f * (0.3f + 0.7f * load_frac); // 0.012A min up to 0.04A near high load
        float jitter = frand_range(-mag, mag);
        float i_out = clampf(obj->current_a + jitter, 0.01f, 6.5f);
        return anjay_ret_float(ctx, i_out);
    }
    case RID_ACTIVE_POWER:
        {
            // Active power jitter: ±0.02 kW or 1.5% of value (may reveal slight swings)
            float base = obj->active_power_kw;
            float mag = fmaxf(0.02f, base * 0.015f);
            float val = clampf(base + frand_range(-mag, mag), 0.0f, 6.0f);
            return anjay_ret_float(ctx, val);
        }
    case RID_REACTIVE_POWER:
        {
            float base = obj->reactive_power_kvar;
            float mag = fmaxf(0.015f, fabsf(base) * 0.02f);
            // reactive can be slightly negative (capacitive); keep within +/-3 kvar realistic single-phase small system
            float val = clampf(base + frand_range(-mag, mag), -3.0f, 3.0f);
            return anjay_ret_float(ctx, val);
        }
    case RID_INDUCTIVE_REACTIVE_POWER:
        {
            float base = obj->inductive_reactive_power_kvar;
            float mag = fmaxf(0.010f, base * 0.02f);
            float val = clampf(base + frand_range(-mag, mag), 0.0f, 3.0f);
            return anjay_ret_float(ctx, val);
        }
    case RID_CAPACITIVE_REACTIVE_POWER:
        {
            float base = obj->capacitive_reactive_power_kvar;
            float mag = fmaxf(0.010f, base * 0.03f);
            float val = clampf(base + frand_range(-mag, mag), 0.0f, 3.0f);
            return anjay_ret_float(ctx, val);
        }
    case RID_APPARENT_POWER:
        {
            float base = obj->apparent_power_kva;
            float mag = fmaxf(0.020f, base * 0.012f);
            float val = clampf(base + frand_range(-mag, mag), 0.0f, 6.5f);
            return anjay_ret_float(ctx, val);
        }
    case RID_POWER_FACTOR:
        {
            float base = obj->power_factor;
            float mag = 0.0035f + (1.0f - base) * 0.004f; // un poco más jitter si PF no es cercano a 1
            float val = clampf(base + frand_range(-mag, mag), 0.40f, 0.999f);
            return anjay_ret_float(ctx, val);
        }
    case RID_THD_V:
        return anjay_ret_float(ctx, obj->thd_v);
    case RID_THD_A:
        return anjay_ret_float(ctx, obj->thd_a);
    case RID_ACTIVE_ENERGY:
        {
            // Monotonic micro-increment: simulate meter register resolution (~0.001 kWh)
            static double last_e_kwh = -1.0;
            double base = obj->active_energy_kwh;
            if (last_e_kwh < 0.0) { last_e_kwh = base; }
            // If base advanced, sync; else add a tiny synthetic increment 0.0002..0.0006
            if (base > last_e_kwh) {
                last_e_kwh = base;
            } else {
                last_e_kwh += frand_range(0.0002f, 0.0006f);
            }
            return anjay_ret_float(ctx, (float) last_e_kwh);
        }
    case RID_REACTIVE_ENERGY:
        {
            static double last_e_kvarh = -1.0;
            double base = obj->reactive_energy_kvarh;
            if (last_e_kvarh < 0.0) { last_e_kvarh = base; }
            if (base > last_e_kvarh) { last_e_kvarh = base; }
            else { last_e_kvarh += frand_range(0.0001f, 0.0003f); }
            return anjay_ret_float(ctx, (float) last_e_kvarh);
        }
    case RID_APPARENT_ENERGY:
        {
            static double last_e_kvah = -1.0;
            double base = obj->apparent_energy_kvah;
            if (last_e_kvah < 0.0) { last_e_kvah = base; }
            if (base > last_e_kvah) { last_e_kvah = base; }
            else { last_e_kvah += frand_range(0.00025f, 0.0007f); }
            return anjay_ret_float(ctx, (float) last_e_kvah);
        }
    case RID_FREQUENCY:
        {
            float base = obj->frequency_hz;
            float val = clampf(base + frand_range(-0.005f, 0.005f), 59.95f, 60.05f);
            return anjay_ret_float(ctx, val);
        }
    case RID_SIM_MODE:
        return anjay_ret_i32(ctx, obj->dynamic_mode ? 1 : 0);
    case RID_UPDATE_PERIOD:
        return anjay_ret_i32(ctx, (int32_t) obj->update_period_sec);
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int resource_write(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
                          anjay_iid_t iid, anjay_rid_t rid, anjay_riid_t riid,
                          anjay_input_ctx_t *in_ctx) {
    (void) anjay; (void) iid; (void) riid;
    sm_ctx_t *obj = AVS_CONTAINER_OF(def, sm_ctx_t, def);
    switch (rid) {
    case RID_SIM_MODE: {
        int32_t v = 0;
        int res = anjay_get_i32(in_ctx, &v);
        if (res) return res;
        if (v != 0 && v != 1) return ANJAY_ERR_BAD_REQUEST;
        bool new_mode = (v == 1);
        if (new_mode != obj->dynamic_mode) {
            obj->dynamic_mode = new_mode;
            ESP_LOGI(TAG_SM, "Simulation mode changed to %s", new_mode ? "dynamic" : "periodic");
            obj->last_update = xTaskGetTickCount(); // reset timer so next update integra rapido
            sm_sync_attrs(anjay); // adjust pmin/pmax if needed
            anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_SIM_MODE);
        }
        return 0;
    }
    case RID_UPDATE_PERIOD: {
        int32_t v = 0;
        int res = anjay_get_i32(in_ctx, &v);
        if (res) return res;
        if (v < 1 || v > 3600) return ANJAY_ERR_BAD_REQUEST;
        if ((uint32_t) v != obj->update_period_sec) {
            obj->update_period_sec = (uint32_t) v;
            ESP_LOGI(TAG_SM, "Update period changed to %ld s", (long) v);
            obj->last_update = xTaskGetTickCount();
            sm_sync_attrs(anjay);
            anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_UPDATE_PERIOD);
        }
        return 0;
    }
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static const anjay_dm_object_def_t OBJ_DEF = {
    .oid = OID_SMART_METER,
    .version = "2.0",
    .handlers = {
        .list_instances = list_instances,
        .list_resources = list_resources,
        .resource_read = resource_read,
        .resource_write = resource_write,
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

    // Energies start at 0 (will load from persistence for active import/export)
    g_sm.active_energy_kwh = 0.0f;
    g_sm.reactive_energy_kvarh = 0.0f;
    g_sm.apparent_energy_kvah = 0.0f;
    energy_acc_init(&g_sm.energy_acc);
    g_sm.active_energy_kwh = (float) g_sm.energy_acc.kwh_import; // map import to active_energy

    // THD (fractions)
    g_sm.thd_v = 0.02f; // 2%
    g_sm.thd_a = 0.03f; // 3%

    g_sm.last_update = xTaskGetTickCount();
    g_sm.last_dyn_notify = g_sm.last_update;
    g_sm.last_model_tick = g_sm.last_update;
    g_sm.attrs_initialized = false;
    g_sm.first_notify_done = false;
    // runtime init: enable dynamic mode by default for visible variation; shorter update period
    g_sm.dynamic_mode = true;
    g_sm.update_period_sec = 10;
    ESP_LOGI(TAG_SM, "Smart Meter dynamic_mode=ON, update_period=%us", (unsigned) g_sm.update_period_sec);
    ESP_LOGI(TAG_SM, "Smart Meter(10243) created");
    return &g_sm.def;
}

void smart_meter_object_release(const anjay_dm_object_def_t *const *obj) {
    (void) obj; // static instance; nothing to free
}

void smart_meter_object_update(anjay_t *anjay, const anjay_dm_object_def_t *const *obj) {
    (void) obj;
    if (!anjay) { return; }
    if (!g_sm.attrs_initialized) {
        sm_sync_attrs(anjay);
    }
    const TickType_t now = xTaskGetTickCount();
    const uint32_t period_ticks = pdMS_TO_TICKS(g_sm.update_period_sec * 1000ULL);
    TickType_t dt_ticks = (g_sm.last_update == 0) ? 0 : (now - g_sm.last_update);
    bool do_periodic = false;
    if (g_sm.last_update == 0 || dt_ticks >= period_ticks) {
        do_periodic = true;
    }
    // Fast dynamic notification every 1s (adjustable) when in dynamic mode
    const TickType_t dyn_interval_ticks = pdMS_TO_TICKS(1000);
    bool do_fast_dyn = false;
    if (g_sm.dynamic_mode && (now - g_sm.last_dyn_notify) >= dyn_interval_ticks) {
        do_fast_dyn = true;
        g_sm.last_dyn_notify = now;
    }
    // Always evolve instantaneous state (even if no notify) so reads change over time.
    TickType_t dt_model_ticks = now - g_sm.last_model_tick;
    g_sm.last_model_tick = now;
    if (do_periodic) {
        g_sm.last_update = now;
    }
    float dt_hours = do_periodic ? ((float) dt_ticks / (float) configTICK_RATE_HZ / 3600.0f) : 0.0f;
    float dt_model_seconds = (float) dt_model_ticks / (float) configTICK_RATE_HZ;
    // For visibility: if in dynamic fast notify path and scheduler interval < ~1s, upscale evolution to 1s equivalent
    float evolve_seconds = dt_model_seconds;
    if (g_sm.dynamic_mode && do_fast_dyn && evolve_seconds < 0.9f) {
        evolve_seconds = 1.0f; // amplify evolution so differences exceed deltas
    }

    // --- NUEVO MODELO REALISTA DE MEDIDOR MONOFÁSICO ---
    // Objetivos:
    //  - Perfil diario (base load) con pico nocturno/vespertino
    //  - Eventos de carga (electrodomésticos) de duración finita
    //  - Sag de tensión dependiente de corriente
    //  - Factor de potencia dependiente de nivel de carga
    //  - THD correlacionado inversamente con PF / sobrecargas
    //  - Frecuencia casi estable con jitter mínimo

    // Estado persistente para eventos y reloj simulado
    typedef struct {
        float extra_current;      // A adicionales por evento
        float remaining_seconds;  // duración restante
    } load_event_t;
    static load_event_t s_event = { 0.0f, 0.0f };
    static bool s_init = false;
    static float sim_seconds = 0.0f; // reloj interno acumulado
    if (!s_init) { s_init = true; sim_seconds = 0.0f; }
    sim_seconds += evolve_seconds; // avanza siempre para coherencia (amplified if needed)

    // Usar nuevo modelo de potencia activa (kW) con random-walk y picos
    float modeled_active_kw = active_power_kw(sim_seconds);

    // Segundo del día (0..86400)
    const float day_seconds = fmodf(sim_seconds, 86400.0f);

    // Perfil diario (normalizado 0..1): baja madrugada, sube mañana, pico tarde-noche
    // Usamos suma de dos gaussians + offset mínimo
    float t = day_seconds / 86400.0f; // 0..1
    // Dos picos centrados ~07:30 (0.3125) y 20:00 (0.8333)
    float peak_morning = expf(-160.0f * (t - 0.3125f) * (t - 0.3125f));
    float peak_evening = expf(-90.0f * (t - 0.8333f) * (t - 0.8333f));
    float base_curve = 0.25f + 0.55f * (peak_morning + 1.3f * peak_evening);
    base_curve = clampf(base_curve, 0.20f, 1.10f);

    // Corriente base nominal (por ejemplo 5 A pico típico residencial monofásico)
    const float rated_current = 5.0f; // A
    float base_current = rated_current * base_curve;

    // Ruido aleatorio suave (±6%)
    base_current *= (1.0f + frand_range(-0.06f, 0.06f));

    // Gestionar evento de carga (lavadora, microondas, etc.)
    if (s_event.remaining_seconds > 0.0f) {
    s_event.remaining_seconds -= evolve_seconds;
        if (s_event.remaining_seconds <= 0.0f) { s_event.extra_current = 0.0f; }
    } else {
        // Probabilidad de iniciar evento en actualización periódica (~1 cada 5-10 min)
        if (do_periodic && (esp_random() % 40u) == 0u) {
            s_event.extra_current = frand_range(0.8f, 2.5f); // A
            s_event.remaining_seconds = frand_range(60.0f, 600.0f); // 1 a 10 minutos
        }
    }

    float total_current = base_current + s_event.extra_current;
    total_current = clampf(total_current, 0.05f, 6.0f); // límites físicos asumidos

    // Factor de potencia modelado: empeora a cargas muy bajas (no lineales) y mejora medio-alto
    // pf = pf_max - k * exp(-I / I0)
    const float pf_max = 0.985f;
    const float k_pf = 0.25f;
    const float I0 = 0.9f;
    float model_pf = pf_max - k_pf * expf(-total_current / I0);
    // Ruido leve PF ±0.01
    model_pf += frand_range(-0.01f, 0.01f);
    model_pf = clampf(model_pf, 0.55f, 0.995f);

    // Frecuencia casi estable
    float new_freq = 60.0f + frand_range(-0.02f, 0.02f);

    // Tensión base 230 ±2 V, caída hasta 8 V en máxima carga proporcional a (I / 6A)
    float voltage_base = 230.0f + frand_range(-2.0f, 2.0f);
    float sag = 8.0f * (total_current / 6.0f); // hasta 8V de caída en 6A
    float new_voltage = voltage_base - sag + frand_range(-0.4f, 0.4f);
    new_voltage = clampf(new_voltage, 205.0f, 255.0f);

    // Potencias
    const float s_kva = (new_voltage * total_current) / 1000.0f; // V*A -> kVA
    // sustituir p_kw base por modelo activo, pero consistente con límites físicos del circuito
    const float p_kw = fminf(modeled_active_kw, s_kva); // no exceder aparente
    // Reactiva: componente imaginaria
    const float q_kvar_mag = s_kva * sqrtf(fmaxf(0.0f, 1.0f - model_pf * model_pf));
    // Decidir inductivo/capacitivo: inductivo la mayoría del tiempo, ocasional capacitivo pequeño
    bool inductive = true;
    if ((esp_random() % 200u) == 0u) { inductive = false; } // ~0.5% de lecturas capactivas
    float new_q_kvar = q_kvar_mag * (inductive ? 1.0f : -1.0f);
    float new_q_ind_kvar = inductive ? q_kvar_mag : 0.0f;
    float new_q_cap_kvar = inductive ? 0.0f : q_kvar_mag;

    // THD dependiente de PF y sobrecarga relativa
    float load_frac = total_current / rated_current; // >1 posible con eventos
    float new_thd_v = clampf(0.012f + 0.030f * (1.0f - model_pf) + 0.010f * fmaxf(0.0f, load_frac - 1.0f) + frand_range(-0.003f, 0.003f), 0.005f, 0.08f);
    float new_thd_a = clampf(0.018f + 0.050f * (1.0f - model_pf) + 0.015f * fmaxf(0.0f, load_frac - 1.0f) + frand_range(-0.005f, 0.005f), 0.010f, 0.15f);

    float new_current = total_current;
    float new_pf = model_pf;

    // Aparente/activa/reactiva ya calculadas (s_kva, p_kw, q_kvar)
    // Integración de energía (solo en actualización periódica)
    float new_e_kwh = g_sm.active_energy_kwh + fmaxf(0.0f, p_kw) * dt_hours;
    float new_e_kvarh = g_sm.reactive_energy_kvarh + fabsf(new_q_kvar) * dt_hours;
    float new_e_kvah = g_sm.apparent_energy_kvah + fmaxf(0.0f, s_kva) * dt_hours;

    // Always update instantaneous snapshot so reads see gradual drift
    {
        g_sm.voltage_v = new_voltage;
        g_sm.frequency_hz = new_freq;
        g_sm.power_factor = new_pf;
        g_sm.current_a = new_current;
        g_sm.active_power_kw = p_kw;
        g_sm.reactive_power_kvar = new_q_kvar;
        g_sm.inductive_reactive_power_kvar = new_q_ind_kvar;
        g_sm.capacitive_reactive_power_kvar = new_q_cap_kvar;
        g_sm.apparent_power_kva = s_kva;
        g_sm.thd_v = new_thd_v;
        g_sm.thd_a = new_thd_a;
    }
    if (do_periodic) {
        // Integrate energies sólo en el periodo principal
    g_sm.active_energy_kwh = new_e_kwh;
        g_sm.reactive_energy_kvarh = new_e_kvarh;
        g_sm.apparent_energy_kvah = new_e_kvah;
        ESP_LOGD(TAG_SM, "periodic update integrated dt_h=%.6f V=%.1f I=%.2f P=%.3f PF=%.3f E=%.4f", (double) dt_hours, (double) g_sm.voltage_v, (double) g_sm.current_a, (double) g_sm.active_power_kw, (double) g_sm.power_factor, (double) g_sm.active_energy_kwh);
    // Persist import/export energies via accumulator (apparent sign logic: assume no export for now)
    energy_acc_add(&g_sm.energy_acc, p_kw, dt_ticks / (double) configTICK_RATE_HZ, sim_seconds);
    // Keep active_energy_kwh in sync with persisted value
    g_sm.active_energy_kwh = (float) g_sm.energy_acc.kwh_import;
    } else if (g_sm.dynamic_mode && do_fast_dyn) {
        // Inject minimal visible jitter if change would be too small for server display rounding
        static bool s_toggle = false; s_toggle = !s_toggle; float sign = s_toggle ? 1.f : -1.f;
        if (fabsf(g_sm.active_power_kw - g_sm.ln_active_power_kw) < 0.002f) {
            g_sm.active_power_kw += sign * 0.012f; // ensure >=0.012 kW delta
        }
        if (fabsf(g_sm.current_a - g_sm.ln_current_a) < 0.003f) {
            g_sm.current_a += sign * 0.02f;
            if (g_sm.current_a < 0.01f) g_sm.current_a = 0.01f;
        }
        if (fabsf(g_sm.voltage_v - g_sm.ln_voltage_v) < 0.05f) {
            g_sm.voltage_v += sign * 0.12f;
        }
        ESP_LOGD(TAG_SM, "dyn update V=%.1f I=%.2f P=%.3f PF=%.3f", (double) g_sm.voltage_v, (double) g_sm.current_a, (double) g_sm.active_power_kw, (double) g_sm.power_factor);
    }

    if (do_periodic || (g_sm.dynamic_mode && do_fast_dyn)) {
        bool first = !g_sm.first_notify_done;
        if (g_sm.dynamic_mode && do_fast_dyn && !do_periodic) {
            // Unconditional notify instantaneous metrics to guarantee visible change each second
            anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_TENSION);        g_sm.ln_voltage_v = g_sm.voltage_v;
            anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_CURRENT);        g_sm.ln_current_a = g_sm.current_a;
            anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_ACTIVE_POWER);   g_sm.ln_active_power_kw = g_sm.active_power_kw;
            anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_REACTIVE_POWER); g_sm.ln_reactive_power_kvar = g_sm.reactive_power_kvar;
            anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_INDUCTIVE_REACTIVE_POWER); g_sm.ln_inductive_reactive_power_kvar = g_sm.inductive_reactive_power_kvar;
            anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_CAPACITIVE_REACTIVE_POWER); g_sm.ln_capacitive_reactive_power_kvar = g_sm.capacitive_reactive_power_kvar;
            anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_APPARENT_POWER); g_sm.ln_apparent_power_kva = g_sm.apparent_power_kva;
            anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_POWER_FACTOR);   g_sm.ln_power_factor = g_sm.power_factor;
            anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_THD_V);          g_sm.ln_thd_v = g_sm.thd_v;
            anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_THD_A);          g_sm.ln_thd_a = g_sm.thd_a;
            anjay_notify_changed(anjay, OID_SMART_METER, 0, RID_FREQUENCY);      g_sm.ln_frequency_hz = g_sm.frequency_hz;
            if (first) { g_sm.first_notify_done = true; }
        } else {
#define SM_MAYBE_NOTIFY(RID_, CURR, LAST, DELTA) \
    do { float _c = (CURR); if (first || fabsf(_c - (LAST)) >= (DELTA)) { (LAST) = _c; anjay_notify_changed(anjay, OID_SMART_METER, 0, (RID_)); } } while(0)
            SM_MAYBE_NOTIFY(RID_TENSION, g_sm.voltage_v, g_sm.ln_voltage_v, SM_DELTA_VOLTAGE);
            SM_MAYBE_NOTIFY(RID_CURRENT, g_sm.current_a, g_sm.ln_current_a, SM_DELTA_CURRENT);
            SM_MAYBE_NOTIFY(RID_ACTIVE_POWER, g_sm.active_power_kw, g_sm.ln_active_power_kw, SM_DELTA_POWER);
            SM_MAYBE_NOTIFY(RID_REACTIVE_POWER, g_sm.reactive_power_kvar, g_sm.ln_reactive_power_kvar, SM_DELTA_POWER);
            SM_MAYBE_NOTIFY(RID_INDUCTIVE_REACTIVE_POWER, g_sm.inductive_reactive_power_kvar, g_sm.ln_inductive_reactive_power_kvar, SM_DELTA_POWER);
            SM_MAYBE_NOTIFY(RID_CAPACITIVE_REACTIVE_POWER, g_sm.capacitive_reactive_power_kvar, g_sm.ln_capacitive_reactive_power_kvar, SM_DELTA_POWER);
            SM_MAYBE_NOTIFY(RID_APPARENT_POWER, g_sm.apparent_power_kva, g_sm.ln_apparent_power_kva, SM_DELTA_POWER);
            SM_MAYBE_NOTIFY(RID_POWER_FACTOR, g_sm.power_factor, g_sm.ln_power_factor, SM_DELTA_PF);
            SM_MAYBE_NOTIFY(RID_THD_V, g_sm.thd_v, g_sm.ln_thd_v, SM_DELTA_THD);
            SM_MAYBE_NOTIFY(RID_THD_A, g_sm.thd_a, g_sm.ln_thd_a, SM_DELTA_THD);
            SM_MAYBE_NOTIFY(RID_FREQUENCY, g_sm.frequency_hz, g_sm.ln_frequency_hz, SM_DELTA_FREQ);
            if (do_periodic) {
                SM_MAYBE_NOTIFY(RID_ACTIVE_ENERGY, g_sm.active_energy_kwh, g_sm.ln_active_energy_kwh, SM_DELTA_ENERGY);
                SM_MAYBE_NOTIFY(RID_REACTIVE_ENERGY, g_sm.reactive_energy_kvarh, g_sm.ln_reactive_energy_kvarh, SM_DELTA_ENERGY);
                SM_MAYBE_NOTIFY(RID_APPARENT_ENERGY, g_sm.apparent_energy_kvah, g_sm.ln_apparent_energy_kvah, SM_DELTA_ENERGY);
            }
            if (first) { g_sm.first_notify_done = true; }
#undef SM_MAYBE_NOTIFY
        }
    }
}
