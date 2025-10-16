#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/*
 * active_power_kw(t_seconds)
 *  - Diurnal baseline defined by 24 hourly values (kW) with linear interpolation
 *  - Bounded random walk (step ±0.1 kW each call, clamped 0..6 kW)
 *  - Transient spikes: probability 0.05 per minute to start; magnitude 1–3 kW, duration 30–90 s
 *  - Smoothed output via EMA (alpha=0.3)
 *  - Thread-safe for single-caller (not reentrant); maintains internal static state
 */
float active_power_kw(double t_seconds);

#ifdef __cplusplus
}
#endif
