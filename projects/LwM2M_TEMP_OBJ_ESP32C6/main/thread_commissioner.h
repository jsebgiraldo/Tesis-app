// OpenThread Commissioner for LwM2M nodes
// Handles commissioning of new Thread devices into the network

#ifndef THREAD_COMMISSIONER_H
#define THREAD_COMMISSIONER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize OpenThread commissioner
 * 
 * @return 0 on success, negative on error
 */
int thread_commissioner_init(void);

/**
 * @brief Start commissioner role
 * 
 * Enables the device to commission new Thread nodes
 * 
 * @return 0 on success, negative on error
 */
int thread_commissioner_start(void);

/**
 * @brief Stop commissioner role
 * 
 * @return 0 on success, negative on error
 */
int thread_commissioner_stop(void);

/**
 * @brief Add joiner with PSKd
 * 
 * @param eui64 EUI-64 of the joining device (NULL for any joiner)
 * @param pskd Pre-Shared Key for Device (6-32 uppercase alphanumeric characters)
 * @param timeout Timeout in seconds (0 for no timeout)
 * 
 * @return 0 on success, negative on error
 */
int thread_commissioner_add_joiner(const uint8_t *eui64, const char *pskd, uint32_t timeout);

/**
 * @brief Remove joiner
 * 
 * @param eui64 EUI-64 of the joining device (NULL to remove any joiner)
 * 
 * @return 0 on success, negative on error
 */
int thread_commissioner_remove_joiner(const uint8_t *eui64);

/**
 * @brief Get commissioner state
 * 
 * @return true if commissioner is active, false otherwise
 */
bool thread_commissioner_is_active(void);

/**
 * @brief Get network credentials for display/sharing
 * 
 * @param network_name Output buffer for network name (min 17 bytes)
 * @param network_key Output buffer for network key hex (min 33 bytes)
 * @param panid Output buffer for PAN ID
 * @param channel Output for channel number
 * 
 * @return 0 on success, negative on error
 */
int thread_commissioner_get_credentials(char *network_name, char *network_key, 
                                        uint16_t *panid, uint8_t *channel);

/**
 * @brief Commissioner event callback type
 */
typedef void (*thread_commissioner_event_cb_t)(const char *event, const void *data);

/**
 * @brief Register event callback
 * 
 * @param callback Callback function to receive commissioner events
 */
void thread_commissioner_register_callback(thread_commissioner_event_cb_t callback);

#ifdef __cplusplus
}
#endif

#endif // THREAD_COMMISSIONER_H
