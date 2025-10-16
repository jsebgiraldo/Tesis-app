#ifndef SERVER_OBJECT_CUSTOM_H
#define SERVER_OBJECT_CUSTOM_H

#include <anjay/anjay.h>

/**
 * @brief Creates a custom LwM2M Server object with explicit version 1.1
 * @param ssid Short Server ID
 * @param lifetime Server lifetime in seconds
 * @param default_min_period Default minimum period for observations
 * @param default_max_period Default maximum period for observations
 * @param binding Binding mode (e.g., "U" for UDP)
 * @return Pointer to the object definition, or NULL on failure
 */
const anjay_dm_object_def_t **server_object_custom_create(
    anjay_ssid_t ssid,
    int32_t lifetime,
    int32_t default_min_period,
    int32_t default_max_period,
    const char *binding);

/**
 * @brief Releases the custom Server object
 * @param def Pointer to the object definition
 */
void server_object_custom_release(const anjay_dm_object_def_t **def);

/**
 * @brief Updates the custom Server object (called periodically)
 * @param anjay Anjay instance
 * @param def Pointer to the object definition
 */
void server_object_custom_update(anjay_t *anjay, const anjay_dm_object_def_t *const *def);

#endif // SERVER_OBJECT_CUSTOM_H
