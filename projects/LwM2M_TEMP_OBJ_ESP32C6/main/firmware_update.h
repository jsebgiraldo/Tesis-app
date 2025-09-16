#pragma once

#include <stdbool.h>
#include <anjay/anjay.h>

// Installs the Anjay Firmware Update object and initializes internal state.
// Returns 0 on success, negative value on error.
int fw_update_install(anjay_t *anjay);

// Returns true if a firmware update has been requested and device should reboot.
bool fw_update_requested(void);

// Performs reboot to complete the firmware upgrade (after successful download).
void fw_update_reboot(void);
