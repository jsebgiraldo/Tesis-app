#pragma once
#include <anjay/anjay.h>

// Minimal LwM2M Location (Object 6) implementation
// Resources:
//  - 0: Latitude (float)
//  - 1: Longitude (float)
//  - 5: Timestamp (time)
// Instance: single (iid=0)

#ifdef __cplusplus
extern "C" {
#endif

const anjay_dm_object_def_t **location_object_create(void);
void location_object_release(const anjay_dm_object_def_t **def);
void location_object_update(anjay_t *anjay, const anjay_dm_object_def_t **def);

#ifdef __cplusplus
}
#endif
