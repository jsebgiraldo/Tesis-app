#pragma once
#include <anjay/anjay.h>

// Minimal BinaryAppDataContainer (Object 19) with 2 instances used by TB:
//  - iid 65533 for Firmware metadata
//  - iid 65534 for Software metadata
// Resources implemented:
//  0: Data (opaque, R/W)
//  2: Data Creation Time (time, R/W)
//  3: Data Description (string, R/W)
//  4: Data Format (string, R/W)
//  5: App ID (string, R/W)

#ifdef __cplusplus
extern "C" {
#endif

const anjay_dm_object_def_t **bac19_object_create(void);
void bac19_object_release(const anjay_dm_object_def_t **def);

#ifdef __cplusplus
}
#endif
