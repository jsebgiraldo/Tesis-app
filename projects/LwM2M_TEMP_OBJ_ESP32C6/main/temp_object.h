#pragma once

#include <anjay/dm.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns the Anjay object definition pointer for IPSO Temperature (OID 3303)
// Caller shall pass this directly to anjay_register_object().
const anjay_dm_object_def_t *const *temp_object_def(void);

#ifdef __cplusplus
}
#endif
