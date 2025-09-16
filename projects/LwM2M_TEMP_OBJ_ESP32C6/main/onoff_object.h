#pragma once

#include <anjay/anjay.h>

#ifdef __cplusplus
extern "C" {
#endif

const anjay_dm_object_def_t *const *onoff_object_def(void);
void onoff_object_update(anjay_t *anjay);
void onoff_object_set(bool state);

#ifdef __cplusplus
}
#endif
