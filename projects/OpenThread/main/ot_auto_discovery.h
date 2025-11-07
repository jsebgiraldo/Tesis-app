/* OpenThread Auto-Discovery and Network Manager */

#ifndef OT_AUTO_DISCOVERY_H
#define OT_AUTO_DISCOVERY_H

#include "esp_openthread.h"
#include "openthread/instance.h"
#include "openthread/thread.h"
#include "openthread/dns_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

// Events for the auto-discovery state machine
#define NETWORK_SCAN_DONE_BIT       BIT0
#define NETWORK_ATTACHED_BIT        BIT1
#define SERVICES_DISCOVERED_BIT     BIT2
#define PING_TEST_DONE_BIT         BIT3

// Structure to store discovered service information
typedef struct {
    char hostname[64];
    char service_name[32];
    char service_type[32];
    uint16_t port;
    char ipv6_addr[64];
    bool ping_success;
    uint32_t ping_time_ms;
} discovered_service_t;

// Structure to store network scan results
typedef struct {
    uint16_t panid;
    uint64_t ext_panid;
    uint8_t channel;
    int8_t rssi;
    char network_name[32];
    bool joinable;
} thread_network_t;

// Auto-discovery configuration
typedef struct {
    bool auto_join_enabled;
    bool auto_discover_services;
    bool ping_discovered_services;
    uint32_t scan_timeout_ms;
    uint32_t join_timeout_ms;
    uint32_t discovery_timeout_ms;
    int8_t min_rssi_threshold;
    
    // Border Router network configuration
    char network_name[32];
    uint16_t panid;
    uint8_t channel;
    uint64_t ext_panid;
    uint8_t network_key[16];
    char mesh_prefix[64];  // e.g., "fdca:6fb:455f:9103::"
} auto_discovery_config_t;

/**
 * @brief Initialize the auto-discovery system
 * @param config Configuration parameters
 * @return ESP_OK on success
 */
esp_err_t ot_auto_discovery_init(const auto_discovery_config_t *config);

/**
 * @brief Start the auto-discovery process
 * This will scan for networks, join if possible, discover services, and test connectivity
 * @return ESP_OK on success
 */
esp_err_t ot_auto_discovery_start(void);

/**
 * @brief Stop the auto-discovery process
 * @return ESP_OK on success
 */
esp_err_t ot_auto_discovery_stop(void);

/**
 * @brief Get the list of discovered networks
 * @param networks Array to store network information
 * @param max_networks Maximum number of networks to return
 * @return Number of networks found
 */
int ot_auto_discovery_get_networks(thread_network_t *networks, int max_networks);

/**
 * @brief Get the list of discovered services
 * @param services Array to store service information
 * @param max_services Maximum number of services to return
 * @return Number of services found
 */
int ot_auto_discovery_get_services(discovered_service_t *services, int max_services);

/**
 * @brief Get current auto-discovery state
 * @return Current state as string
 */
const char* ot_auto_discovery_get_state(void);

/**
 * @brief Check if connected to Thread network
 * @return true if connected
 */
bool ot_auto_discovery_is_connected(void);

/**
 * @brief Get best service for LwM2M connection
 * @param service Pointer to store best service information
 * @return true if found
 */
bool ot_auto_discovery_get_best_lwm2m_service(discovered_service_t *service);

#ifdef __cplusplus
}
#endif

#endif /* OT_AUTO_DISCOVERY_H */