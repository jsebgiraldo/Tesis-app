#pragma once

#include <anjay/dm.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns the Anjay object definition pointer for Single-Phase Power Meter (OID 10243)
// Caller shall pass this directly to anjay_register_object().
const anjay_dm_object_def_t *const *smart_meter_object_def(void);

#ifdef __cplusplus
}
#endif

