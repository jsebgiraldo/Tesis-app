#pragma once

#include <anjay/anjay.h>

#ifdef __cplusplus
extern "C" {
#endif

const anjay_dm_object_def_t *const *humidity_object_def(void);
void humidity_object_update(anjay_t *anjay);

#ifdef __cplusplus
}
#endif
