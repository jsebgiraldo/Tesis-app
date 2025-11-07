/* OpenThread Auto-Discovery and Network Manager Implementation - Simplified Version */

#include "ot_auto_discovery.h"
#include "esp_log.h"
#include "esp_openthread_lock.h"
#include "openthread/thread.h"
#include "openthread/dataset.h"
#include "openthread/icmp6.h"
#include "openthread/netdata.h"
#include <string.h>
#include <stdio.h>

#define TAG "ot_auto_discovery"
#define MAX_NETWORKS 10
#define MAX_SERVICES 20
#define PING_TIMEOUT_MS 5000

// Auto-discovery states
typedef enum {
    STATE_IDLE = 0,
    STATE_SCANNING,
    STATE_JOINING,
    STATE_CONNECTED,
    STATE_DISCOVERING_SERVICES,
    STATE_TESTING_CONNECTIVITY,
    STATE_COMPLETED,
    STATE_ERROR
} auto_discovery_state_t;

// Global state
static auto_discovery_config_t s_config;
static auto_discovery_state_t s_state = STATE_IDLE;
static EventGroupHandle_t s_event_group;
static TaskHandle_t s_discovery_task;

// Storage for discovered data
static thread_network_t s_discovered_networks[MAX_NETWORKS];
static int s_network_count = 0;
static discovered_service_t s_discovered_services[MAX_SERVICES];
static int s_service_count = 0;

// Forward declarations
static void auto_discovery_task(void *pvParameters);

esp_err_t ot_auto_discovery_init(const auto_discovery_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Configuration is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_config, config, sizeof(auto_discovery_config_t));
    
    // Create event group for synchronization
    s_event_group = xEventGroupCreate();
    if (!s_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    // Reset state
    s_state = STATE_IDLE;
    s_network_count = 0;
    s_service_count = 0;
    memset(s_discovered_networks, 0, sizeof(s_discovered_networks));
    memset(s_discovered_services, 0, sizeof(s_discovered_services));

    ESP_LOGI(TAG, "Auto-discovery initialized");
    return ESP_OK;
}

esp_err_t ot_auto_discovery_start(void)
{
    if (s_state != STATE_IDLE) {
        ESP_LOGW(TAG, "Auto-discovery already running");
        return ESP_ERR_INVALID_STATE;
    }

    // Create the discovery task
    BaseType_t result = xTaskCreate(
        auto_discovery_task,
        "auto_discovery",
        4096,
        NULL,
        5,
        &s_discovery_task
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create discovery task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Auto-discovery started");
    return ESP_OK;
}

esp_err_t ot_auto_discovery_stop(void)
{
    if (s_discovery_task) {
        vTaskDelete(s_discovery_task);
        s_discovery_task = NULL;
    }
    
    s_state = STATE_IDLE;
    ESP_LOGI(TAG, "Auto-discovery stopped");
    return ESP_OK;
}

static void auto_discovery_task(void *pvParameters)
{
    otInstance *instance = esp_openthread_get_instance();
    otError error;

    ESP_LOGI(TAG, "=== AUTO-DISCOVERY STARTING ===");

    // Step 0: Check if already connected or configured
    esp_openthread_lock_acquire(portMAX_DELAY);
    otDeviceRole current_role = otThreadGetDeviceRole(instance);
    esp_openthread_lock_release();

    ESP_LOGI(TAG, "Current state - Role: %s", 
             current_role == OT_DEVICE_ROLE_DISABLED ? "Disabled" :
             current_role == OT_DEVICE_ROLE_DETACHED ? "Detached" :
             current_role == OT_DEVICE_ROLE_CHILD ? "Child" :
             current_role == OT_DEVICE_ROLE_ROUTER ? "Router" :
             current_role == OT_DEVICE_ROLE_LEADER ? "Leader" : "Unknown");

    // If already connected, skip to service discovery
    if (current_role == OT_DEVICE_ROLE_CHILD || 
        current_role == OT_DEVICE_ROLE_ROUTER || 
        current_role == OT_DEVICE_ROLE_LEADER) {
        ESP_LOGI(TAG, "✅ Already connected to Thread network!");
        s_state = STATE_CONNECTED;
        goto check_services;
    }

    // If Thread is detached, wait a bit for connection
    if (current_role == OT_DEVICE_ROLE_DETACHED) {
        ESP_LOGI(TAG, "Thread is detached, waiting for attachment...");
        s_state = STATE_JOINING;
        
        int retries = 10;  // Wait up to 10 seconds
        while (retries > 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            esp_openthread_lock_acquire(portMAX_DELAY);
            current_role = otThreadGetDeviceRole(instance);
            esp_openthread_lock_release();
            
            if (current_role == OT_DEVICE_ROLE_CHILD || 
                current_role == OT_DEVICE_ROLE_ROUTER || 
                current_role == OT_DEVICE_ROLE_LEADER) {
                ESP_LOGI(TAG, "✅ Successfully attached to network as %s",
                         current_role == OT_DEVICE_ROLE_CHILD ? "Child" :
                         current_role == OT_DEVICE_ROLE_ROUTER ? "Router" : "Leader");
                s_state = STATE_CONNECTED;
                goto check_services;
            }
            
            retries--;
        }
        
        ESP_LOGW(TAG, "Thread enabled but failed to attach, reconfiguring...");
    }

    // Step 1: Skip network scan, configure directly with Border Router credentials
    s_state = STATE_SCANNING;
    ESP_LOGI(TAG, "Skipping network scan - configuring Border Router directly...");

    // Step 2: Configure and join the Border Router network automatically
    if (s_config.auto_join_enabled) {
        s_state = STATE_JOINING;
        ESP_LOGI(TAG, "=== BORDER ROUTER AUTO-JOIN ===");
        ESP_LOGI(TAG, "Network Name: %s", s_config.network_name);
        ESP_LOGI(TAG, "PAN ID: 0x%04X", s_config.panid);
        ESP_LOGI(TAG, "Channel: %d", s_config.channel);
        ESP_LOGI(TAG, "Extended PAN ID: %016llX", s_config.ext_panid);
        ESP_LOGI(TAG, "Mesh Prefix: %s", s_config.mesh_prefix);
        ESP_LOGI(TAG, "Attempting to join Border Router network...");

        // Configure the dataset with Border Router credentials
        esp_openthread_lock_acquire(portMAX_DELAY);
        
        otOperationalDataset dataset;
        memset(&dataset, 0, sizeof(dataset));
        
        // Set Network Name
        size_t network_name_len = strlen(s_config.network_name);
        memcpy(dataset.mNetworkName.m8, s_config.network_name, network_name_len);
        dataset.mComponents.mIsNetworkNamePresent = true;
        
        // Set PAN ID
        dataset.mPanId = s_config.panid;
        dataset.mComponents.mIsPanIdPresent = true;
        
        // Set Channel
        dataset.mChannel = s_config.channel;
        dataset.mComponents.mIsChannelPresent = true;
        
        // Set Extended PAN ID
        memcpy(dataset.mExtendedPanId.m8, &s_config.ext_panid, sizeof(s_config.ext_panid));
        dataset.mComponents.mIsExtendedPanIdPresent = true;
        
        // Set Network Key
        memcpy(dataset.mNetworkKey.m8, s_config.network_key, sizeof(s_config.network_key));
        dataset.mComponents.mIsNetworkKeyPresent = true;
        
        // Set Mesh Local Prefix
        otIp6Address mesh_prefix;
        otIp6AddressFromString(s_config.mesh_prefix, &mesh_prefix);
        memcpy(dataset.mMeshLocalPrefix.m8, mesh_prefix.mFields.m8, 8);
        dataset.mComponents.mIsMeshLocalPrefixPresent = true;
        
        // Set Security Policy
        dataset.mSecurityPolicy.mRotationTime = 672;
        dataset.mSecurityPolicy.mObtainNetworkKeyEnabled = true;
        dataset.mSecurityPolicy.mNativeCommissioningEnabled = true;
        dataset.mSecurityPolicy.mRoutersEnabled = true;
        dataset.mSecurityPolicy.mExternalCommissioningEnabled = true;
        dataset.mSecurityPolicy.mCommercialCommissioningEnabled = false;
        dataset.mSecurityPolicy.mAutonomousEnrollmentEnabled = false;
        dataset.mSecurityPolicy.mNetworkKeyProvisioningEnabled = false;
        dataset.mSecurityPolicy.mTobleLinkEnabled = true;
        dataset.mSecurityPolicy.mNonCcmRoutersEnabled = false;
        dataset.mSecurityPolicy.mVersionThresholdForRouting = 0;
        dataset.mComponents.mIsSecurityPolicyPresent = true;
        
        // Apply the dataset
        error = otDatasetSetActive(instance, &dataset);
        if (error != OT_ERROR_NONE) {
            ESP_LOGE(TAG, "Failed to set dataset: %d", error);
        } else {
            ESP_LOGI(TAG, "Dataset configured successfully");
            
            // Enable IPv6
            error = otIp6SetEnabled(instance, true);
            if (error == OT_ERROR_NONE) {
                ESP_LOGI(TAG, "IPv6 enabled");
                
                // Enable Thread
                error = otThreadSetEnabled(instance, true);
                if (error == OT_ERROR_NONE) {
                    ESP_LOGI(TAG, "Thread enabled - attempting to join network");
                } else {
                    ESP_LOGE(TAG, "Failed to enable Thread: %d", error);
                }
            } else {
                ESP_LOGE(TAG, "Failed to enable IPv6: %d", error);
            }
        }
        
        esp_openthread_lock_release();

        // Wait for connection
        ESP_LOGI(TAG, "Waiting for Thread network attachment...");
        int retries = s_config.join_timeout_ms / 2000;  // Check every 2 seconds
        while (retries > 0) {
            esp_openthread_lock_acquire(portMAX_DELAY);
            otDeviceRole role = otThreadGetDeviceRole(instance);
            esp_openthread_lock_release();

            ESP_LOGI(TAG, "Current role: %s", 
                     role == OT_DEVICE_ROLE_DISABLED ? "Disabled" :
                     role == OT_DEVICE_ROLE_DETACHED ? "Detached" :
                     role == OT_DEVICE_ROLE_CHILD ? "Child" :
                     role == OT_DEVICE_ROLE_ROUTER ? "Router" :
                     role == OT_DEVICE_ROLE_LEADER ? "Leader" : "Unknown");

            if (role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER) {
                s_state = STATE_CONNECTED;
                ESP_LOGI(TAG, "✅ Successfully joined Border Router network as %s!", 
                         role == OT_DEVICE_ROLE_CHILD ? "Child" : "Router");
                
                xEventGroupSetBits(s_event_group, NETWORK_ATTACHED_BIT);
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(2000));
            retries--;
        }

        if (s_state != STATE_CONNECTED) {
            ESP_LOGW(TAG, "❌ Failed to join Border Router network within timeout");
            ESP_LOGW(TAG, "Check that Border Router is running and credentials are correct");
        }
    }

    // Step 3: Check if connected and discover services (simplified)
check_services:
    esp_openthread_lock_acquire(portMAX_DELAY);
    otDeviceRole role = otThreadGetDeviceRole(instance);
    esp_openthread_lock_release();

    if (role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER || role == OT_DEVICE_ROLE_LEADER) {
        if (s_state != STATE_CONNECTED) {
            s_state = STATE_CONNECTED;
            ESP_LOGI(TAG, "✅ Connected to Thread network as %s", 
                     role == OT_DEVICE_ROLE_CHILD ? "Child" :
                     role == OT_DEVICE_ROLE_ROUTER ? "Router" : "Leader");
        }
        
        // Get and display IPv6 addresses
        esp_openthread_lock_acquire(portMAX_DELAY);
        const otNetifAddress *unicastAddrs = otIp6GetUnicastAddresses(instance);
        ESP_LOGI(TAG, "=== ASSIGNED IPv6 ADDRESSES ===");
        int addr_count = 0;
        for (const otNetifAddress *addr = unicastAddrs; addr; addr = addr->mNext) {
            char ipv6_str[64];
            otIp6AddressToString(&addr->mAddress, ipv6_str, sizeof(ipv6_str));
            ESP_LOGI(TAG, "IPv6[%d]: %s", addr_count++, ipv6_str);
        }
        esp_openthread_lock_release();
        
        if (s_config.auto_discover_services) {
            s_state = STATE_DISCOVERING_SERVICES;
            ESP_LOGI(TAG, "Discovering services...");
            ESP_LOGI(TAG, "Use these commands to discover services:");
            ESP_LOGI(TAG, "dns browse _coap._udp.default.service.arpa.");
            ESP_LOGI(TAG, "dns browse _coaps._udp.default.service.arpa.");
            ESP_LOGI(TAG, "Or use simplified commands: 'discover coap', 'discover coaps'");
            
            // Simulate some discovered services for demo
            if (s_service_count < MAX_SERVICES) {
                discovered_service_t *service = &s_discovered_services[s_service_count];
                strcpy(service->service_name, "thingsboard-lwm2m");
                strcpy(service->service_type, "_coap._udp");
                strcpy(service->hostname, "border-router");
                service->port = 5685;
                strcpy(service->ipv6_addr, "fdca:6fb:455f:9103:e33b:33ab:2c9a:b3f2");
                service->ping_success = false;  // Will be tested later
                s_service_count++;
                
                ESP_LOGI(TAG, "Sample service added for LwM2M testing");
            }
        }
    }

    s_state = STATE_COMPLETED;
    ESP_LOGI(TAG, "Auto-discovery process completed");

    // Print summary
    ESP_LOGI(TAG, "=== AUTO-DISCOVERY SUMMARY ===");
    ESP_LOGI(TAG, "Networks found: %d", s_network_count);
    ESP_LOGI(TAG, "Services found: %d", s_service_count);
    ESP_LOGI(TAG, "Connected: %s", ot_auto_discovery_is_connected() ? "Yes" : "No");
    
    for (int i = 0; i < s_network_count; i++) {
        ESP_LOGI(TAG, "Network %d: %s (PAN: 0x%04X, Ch: %d, RSSI: %d, Joinable: %s)",
                 i+1, 
                 s_discovered_networks[i].network_name,
                 s_discovered_networks[i].panid,
                 s_discovered_networks[i].channel,
                 s_discovered_networks[i].rssi,
                 s_discovered_networks[i].joinable ? "Yes" : "No");
    }
    
    for (int i = 0; i < s_service_count; i++) {
        ESP_LOGI(TAG, "Service %d: %s (%s) at %s:%d", 
                 i+1, 
                 s_discovered_services[i].service_name,
                 s_discovered_services[i].service_type,
                 s_discovered_services[i].ipv6_addr,
                 s_discovered_services[i].port);
    }

    s_discovery_task = NULL;
    vTaskDelete(NULL);
}

/* Callback not used in current implementation
static void network_scan_callback(otActiveScanResult *aResult, void *aContext)
{
    if (aResult && s_network_count < MAX_NETWORKS) {
        thread_network_t *network = &s_discovered_networks[s_network_count];
        
        network->panid = aResult->mPanId;
        network->ext_panid = 0;  // Simplified - not available in this structure
        network->channel = aResult->mChannel;
        network->rssi = aResult->mRssi;
        network->joinable = aResult->mIsJoinable;
        
        // Copy network name if available (simplified)
        snprintf(network->network_name, sizeof(network->network_name), "Network_%04X", network->panid);

        ESP_LOGI(TAG, "Found network: %s (PAN: 0x%04X, Ch: %d, RSSI: %d, Joinable: %s)",
                 network->network_name, network->panid, network->channel, network->rssi,
                 network->joinable ? "Yes" : "No");

        s_network_count++;
    } else if (!aResult) {
        // Scan complete
        ESP_LOGI(TAG, "Network scan completed, found %d networks", s_network_count);
        xEventGroupSetBits(s_event_group, NETWORK_SCAN_DONE_BIT);
    }
}
*/

// Public API implementations
int ot_auto_discovery_get_networks(thread_network_t *networks, int max_networks)
{
    int count = s_network_count < max_networks ? s_network_count : max_networks;
    if (networks) {
        memcpy(networks, s_discovered_networks, count * sizeof(thread_network_t));
    }
    return count;
}

int ot_auto_discovery_get_services(discovered_service_t *services, int max_services)
{
    int count = s_service_count < max_services ? s_service_count : max_services;
    if (services) {
        memcpy(services, s_discovered_services, count * sizeof(discovered_service_t));
    }
    return count;
}

const char* ot_auto_discovery_get_state(void)
{
    switch (s_state) {
        case STATE_IDLE: return "Idle";
        case STATE_SCANNING: return "Scanning";
        case STATE_JOINING: return "Joining";
        case STATE_CONNECTED: return "Connected";
        case STATE_DISCOVERING_SERVICES: return "Discovering Services";
        case STATE_TESTING_CONNECTIVITY: return "Testing Connectivity";
        case STATE_COMPLETED: return "Completed";
        case STATE_ERROR: return "Error";
        default: return "Unknown";
    }
}

bool ot_auto_discovery_is_connected(void)
{
    otInstance *instance = esp_openthread_get_instance();
    esp_openthread_lock_acquire(portMAX_DELAY);
    otDeviceRole role = otThreadGetDeviceRole(instance);
    esp_openthread_lock_release();
    
    return (role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER);
}

bool ot_auto_discovery_get_best_lwm2m_service(discovered_service_t *service)
{
    if (!service) return false;
    
    // Find best LwM2M service (CoAP service)
    for (int i = 0; i < s_service_count; i++) {
        discovered_service_t *current = &s_discovered_services[i];
        
        // Look for CoAP services
        if (strstr(current->service_type, "coap") && strlen(current->ipv6_addr) > 0) {
            memcpy(service, current, sizeof(discovered_service_t));
            return true;
        }
    }
    
    return false;
}