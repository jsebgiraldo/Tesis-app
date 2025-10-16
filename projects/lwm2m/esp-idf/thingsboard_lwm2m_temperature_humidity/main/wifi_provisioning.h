#pragma once

#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WiFi provisioning system
 * 
 * This function initializes the WiFi provisioning manager, sets up event handlers,
 * and starts the provisioning service if the device hasn't been provisioned yet.
 * If already provisioned, it will start in normal WiFi station mode.
 */
void wifi_provisioning_init(void);

/**
 * @brief Wait for WiFi connection to be established
 * 
 * This function blocks until the WiFi connection is established, either through
 * provisioning process or normal station connection.
 */
void wifi_provisioning_wait_connected(void);

/**
 * @brief Check if WiFi is connected
 * 
 * @return true if WiFi is connected, false otherwise
 */
bool wifi_provisioning_is_connected(void);

#ifdef __cplusplus
}
#endif
