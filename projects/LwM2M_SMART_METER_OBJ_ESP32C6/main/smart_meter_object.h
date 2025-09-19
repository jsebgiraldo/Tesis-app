#pragma once

#include <anjay/anjay.h>

#ifdef __cplusplus
extern "C" {
#endif

// Custom Smart Meter (Object ID 10243)
// Resource IDs per design:
//   0 Manufacturer (string)
//   1 Model Number (string)
//   2 Serial Number (string)
//   3 Description (string)
//   4 Voltage/Tension (float V)
//   5 Current (float A) [Mandatory]
//   6 Active Power (float kW)
//   7 Reactive Power (float kvar)
//   8 Inductive Reactive Power (float kvar)
//   9 Capacitive Reactive Power (float kvar)
//  10 Apparent Power (float kVA)
//  11 Power Factor (float -1..1)
//  12 THD-V (float /100)
//  13 THD-A (float /100)
//  14 Active Energy (float kWh)
//  15 Reactive Energy (float kvarh)
//  16 Apparent Energy (float kVAh)
//  17 Frequency (float Hz)

const anjay_dm_object_def_t *const *smart_meter_object_create(void);
void smart_meter_object_release(const anjay_dm_object_def_t *const *obj);
void smart_meter_object_update(anjay_t *anjay, const anjay_dm_object_def_t *const *obj);

#ifdef __cplusplus
}
#endif
