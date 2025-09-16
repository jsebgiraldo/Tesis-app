#pragma once

#include <anjay/anjay.h>

#ifdef __cplusplus
extern "C" {
#endif

const anjay_dm_object_def_t *const *connectivity_object_def(void);
void connectivity_object_update(anjay_t *anjay);

#ifdef __cplusplus
}
#endif
