#pragma once

#include <anjay/anjay.h>

#ifdef __cplusplus
extern "C" {
#endif

// Create LwM2M Device (Object 3) with basic resources
const anjay_dm_object_def_t **device_object_create(const char *endpoint_name);

// Release the object definition
void device_object_release(const anjay_dm_object_def_t **def);

// Periodic upkeep
void device_object_update(anjay_t *anjay, const anjay_dm_object_def_t *const *def);

#ifdef __cplusplus
}
#endif
