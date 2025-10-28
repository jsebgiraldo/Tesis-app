// OpenThread Joiner - Para unirse a redes Thread existentes
#ifndef THREAD_JOINER_H
#define THREAD_JOINER_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Event bit for Thread network attachment
 */
#define THREAD_ATTACHED_BIT BIT0

/**
 * @brief Initialize OpenThread joiner
 * 
 * @return 0 on success, negative on error
 */
int thread_joiner_init(void);

/**
 * @brief Start joiner process to join existing Thread network
 * 
 * @param pskd Pre-Shared Key for Device (provided by Commissioner)
 * @param provisioning_url Optional provisioning URL (can be NULL)
 * 
 * @return 0 on success, negative on error
 */
int thread_joiner_start(const char *pskd, const char *provisioning_url);

/**
 * @brief Stop joiner process
 * 
 * @return 0 on success, negative on error
 */
int thread_joiner_stop(void);

/**
 * @brief Get joiner state
 * 
 * @return true if joiner is active/in-progress, false otherwise
 */
bool thread_joiner_is_active(void);

/**
 * @brief Check if device is already attached to Thread network
 * 
 * @return true if attached, false otherwise
 */
bool thread_joiner_is_attached(void);

/**
 * @brief Get device role in Thread network
 * 
 * @return Role string: "disabled", "detached", "child", "router", "leader"
 */
const char* thread_joiner_get_role(void);

/**
 * @brief Get potential Border Router/Leader IPv6 addresses for server discovery
 * 
 * @param addresses Array to store IPv6 addresses (output)
 * @param max_addresses Maximum number of addresses to return
 * @return Number of addresses found (0 if none)
 */
int thread_joiner_get_border_router_candidates(char addresses[][46], int max_addresses);

/**
 * @brief Get event group handle for Thread network events
 * 
 * @return EventGroupHandle_t Event group handle (or NULL if not initialized)
 */
EventGroupHandle_t thread_joiner_get_event_group(void);

/**
 * @brief Joiner event callback type
 */
typedef void (*thread_joiner_event_cb_t)(const char *event, const void *data);

/**
 * @brief Register event callback
 * 
 * @param callback Callback function to receive joiner events
 */
void thread_joiner_register_callback(thread_joiner_event_cb_t callback);

#ifdef __cplusplus
}
#endif

#endif // THREAD_JOINER_H
