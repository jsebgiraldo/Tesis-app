#pragma once

#include <anjay/anjay.h>

#ifdef __cplusplus
extern "C" {
#endif

// Custom Single Phase Power Meter (Object ID 10243)
// Example resources (subset):
//  - 5600 Voltage (V) float
//  - 5601 Current (A) float
//  - 5700 Active Power (W) float
//  - 5805 Power Factor float
//  - 5701 Units (string) for 5700
//  - 5800 Energy (kWh) float (cumulative)
//  - 5850 On/Off (bool) RW (simulated load enable)
//  - 5822 Frequency (Hz) float
//  - 5605 Reset Cumulative Energy (Exec)

const anjay_dm_object_def_t *const *smart_meter_object_create(void);
void smart_meter_object_release(const anjay_dm_object_def_t *const *obj);
void smart_meter_object_update(anjay_t *anjay, const anjay_dm_object_def_t *const *obj);

#ifdef __cplusplus
}
#endif
