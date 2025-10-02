#include "power_model.h"
#include <math.h>
#include <esp_random.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Diurnal baseline (kW) representative residential profile (hour 0..23)
static const float DIURNAL_BASE[24] = {
    0.35f, // 00
    0.30f, // 01
    0.28f, // 02
    0.27f, // 03
    0.27f, // 04
    0.30f, // 05
    0.45f, // 06 - morning ramp
    0.70f, // 07
    0.85f, // 08
    0.75f, // 09
    0.65f, // 10
    0.60f, // 11
    0.55f, // 12
    0.55f, // 13
    0.60f, // 14
    0.70f, // 15
    0.90f, // 16
    1.20f, // 17 - evening cooking start
    1.60f, // 18
    1.90f, // 19 peak
    1.70f, // 20
    1.10f, // 21
    0.70f, // 22
    0.50f  // 23
};

static inline float frand_unit(void) {
    return (float)((double)esp_random() / (double)UINT32_MAX);
}
static inline float frand_range(float a, float b) { return a + (b - a) * frand_unit(); }
static inline float clampf(float x, float lo, float hi) { return fminf(hi, fmaxf(lo, x)); }

float active_power_kw(double t_seconds) {
    // Internal static state
    static bool init = false;
    static float last_output = 0.0f; // after EMA
    static float walk_value = 0.0f;  // before EMA
    static float spike_remaining = 0.0f; // seconds
    static float spike_magnitude = 0.0f; // kW added
    static double last_t = 0.0;

    if (!init) {
        init = true;
        last_t = t_seconds;
        // initialize baseline at current hour
        double hours = fmod(t_seconds / 3600.0, 24.0);
        int h0 = (int)floor(hours) % 24;
        int h1 = (h0 + 1) % 24;
        float frac = (float)(hours - floor(hours));
        float base = DIURNAL_BASE[h0] + (DIURNAL_BASE[h1] - DIURNAL_BASE[h0]) * frac;
        walk_value = base;
        last_output = base;
    }

    double dt = t_seconds - last_t;
    if (dt < 0) { // time reset; reinit
        init = false;
        return active_power_kw(t_seconds);
    }
    if (dt > 5.0) { // large gap safeguard; cap dt for steps logic
        dt = 5.0;
    }
    last_t = t_seconds;

    // Baseline interpolation
    double hours = fmod(t_seconds / 3600.0, 24.0);
    int h0 = (int)floor(hours) % 24;
    int h1 = (h0 + 1) % 24;
    float frac = (float)(hours - floor(hours));
    float baseline = DIURNAL_BASE[h0] + (DIURNAL_BASE[h1] - DIURNAL_BASE[h0]) * frac;

    // Random walk step scaled by dt (assuming typical call ~1s)
    float max_step = 0.1f * (float)dt; // ±0.1 kW per 1s
    float step = frand_range(-max_step, max_step);
    walk_value += step;

    // Mean-revert gently toward baseline to avoid drift
    float revert_strength = 0.02f * (float)dt; // small coefficient
    walk_value += (baseline - walk_value) * revert_strength;

    // Clamp base walk
    walk_value = clampf(walk_value, 0.0f, 6.0f);

    // Spike management
    if (spike_remaining > 0.0f) {
        spike_remaining -= (float)dt;
        if (spike_remaining <= 0.0f) {
            spike_magnitude = 0.0f;
        }
    } else {
        // Probability 0.05 per minute => per second probability p = 0.05/60
        float p = (float)dt * (0.05f / 60.0f);
        if (frand_unit() < p) {
            spike_magnitude = frand_range(1.0f, 3.0f); // kW
            spike_remaining = frand_range(30.0f, 90.0f); // s
        }
    }

    float raw_power = walk_value + spike_magnitude;
    raw_power = clampf(raw_power, 0.0f, 6.0f);

    // EMA smoothing alpha=0.3
    const float alpha = 0.3f;
    last_output = alpha * raw_power + (1.0f - alpha) * last_output;
    return last_output;
}
