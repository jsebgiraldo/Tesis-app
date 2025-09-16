#pragma once

#include <anjay/anjay.h>

// Create LwM2M Device (Object 3) with instance 0 implementing key resources:
// 0/1/2 Manufacturer, Model, Serial; 3 Firmware Version; 4 Reboot (Exec);
// 9 Battery Level; 10 Memory Free; 11 Error Code (multi); 13 Current Time;
// 14 UTC Offset (RW); 15 Timezone (RW); 16 Supported Binding; 17 Device Type;
// 18 Hardware Version; 19 Software Version; 20 Battery Status; 21 Memory Total.
// Serial will be initialised from the provided endpoint name.
const anjay_dm_object_def_t **device_object_create(const char *endpoint_name);

// Release the object definition.
void device_object_release(const anjay_dm_object_def_t **def);

// Periodic upkeep: update dynamic resources and handle pending reboot requests.
void device_object_update(anjay_t *anjay, const anjay_dm_object_def_t *const *def);
