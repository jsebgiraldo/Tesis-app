#pragma once

#include <anjay/anjay.h>
#include <anjay/dm.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns the Anjay object definition pointer for IPSO Temperature (OID 3303)
const anjay_dm_object_def_t *const *temp_object_def(void);

// Periodic update hook to refresh the simulated temperature and trigger notifications
void temp_object_update(anjay_t *anjay);

#ifdef __cplusplus
}
#endif
