/* LwM2M Client for OpenThread using Anjay */

#ifndef LWM2M_CLIENT_H
#define LWM2M_CLIENT_H

#include "esp_err.h"
#include "ot_auto_discovery.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize LwM2M client
 * @return ESP_OK on success
 */
esp_err_t lwm2m_client_init(void);

/**
 * @brief Start LwM2M client (begins registration)
 * @return ESP_OK on success
 */
esp_err_t lwm2m_client_start(void);

/**
 * @brief Stop LwM2M client (deregister)
 * @return ESP_OK on success
 */
esp_err_t lwm2m_client_stop(void);

/**
 * @brief Check if client is registered
 * @return true if registered and running
 */
bool lwm2m_client_is_registered(void);

/**
 * @brief Get state as string
 * @return State name
 */
const char* lwm2m_client_get_state_str(void);

/**
 * @brief Auto-configure from discovered service
 * @param service Discovered LwM2M service
 * @return ESP_OK on success
 */
esp_err_t lwm2m_client_auto_configure(const discovered_service_t *service);

/**
 * @brief Update temperature value (for testing)
 * @param temperature Temperature in Celsius
 * @return ESP_OK on success
 */
esp_err_t lwm2m_client_update_temperature(float temperature);

#ifdef __cplusplus
}
#endif

#endif /* LWM2M_CLIENT_H */
