#ifndef THINGSBOARD_PROVISION_H
#define THINGSBOARD_PROVISION_H

#include <anjay/anjay.h>
#include <stdbool.h>

/**
 * @brief Initialize ThingsBoard provisioning state
 * 
 * This function should be called before starting the LwM2M client
 * to prepare for potential bootstrap/provisioning flow.
 */
void thingsboard_provision_init(void);

/**
 * @brief Check if device is already provisioned
 * @return true if device has valid server credentials, false otherwise
 */
bool thingsboard_is_provisioned(void);

/**
 * @brief Setup bootstrap for ThingsBoard auto-provisioning
 * @param anjay Anjay instance
 * @param endpoint_name Device endpoint name (will be used as device identifier)
 * @return 0 on success, negative on error
 * 
 * This configures the Bootstrap Security object (0) instance
 * for connecting to ThingsBoard Bootstrap Server.
 */
int thingsboard_setup_bootstrap(anjay_t *anjay, const char *endpoint_name);

/**
 * @brief Check if bootstrap process completed successfully
 * @return true if bootstrap finished and device received server config
 */
bool thingsboard_bootstrap_finished(void);

/**
 * @brief Get provisioned server URI after bootstrap
 * @param uri_buf Buffer to store the URI
 * @param buf_size Size of the buffer
 * @return 0 on success, negative if not provisioned yet
 */
int thingsboard_get_provisioned_uri(char *uri_buf, size_t buf_size);

/**
 * @brief Store provisioned credentials to NVS for persistence
 * @return 0 on success, negative on error
 * 
 * Call this after successful bootstrap to persist credentials
 * across reboots.
 */
int thingsboard_save_credentials(void);

/**
 * @brief Load previously provisioned credentials from NVS
 * @param anjay Anjay instance
 * @return 0 on success, negative if no credentials stored
 * 
 * Call this at startup to skip bootstrap if already provisioned.
 */
int thingsboard_load_credentials(anjay_t *anjay);

/**
 * @brief Clear provisioned credentials (factory reset)
 * @return 0 on success
 * 
 * Forces device to re-bootstrap on next startup.
 */
int thingsboard_clear_credentials(void);

#endif // THINGSBOARD_PROVISION_H
