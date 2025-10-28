// OpenThread Joiner Implementation - Join existing Thread networks
#include "thread_joiner.h"
#include "led_status.h"
#include "sdkconfig.h"

#if CONFIG_OPENTHREAD_ENABLED

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"
#include "esp_vfs_eventfd.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "openthread/joiner.h"
#include "openthread/instance.h"
#include "openthread/thread.h"
#include "openthread/dataset.h"
#include "openthread/ip6.h"
#include "openthread/logging.h"

static const char *TAG = "thread_joiner";
static thread_joiner_event_cb_t g_event_callback = NULL;
static bool g_joiner_active = false;
static esp_netif_t *s_openthread_netif = NULL;
static EventGroupHandle_t s_thread_event_group = NULL;

// Joiner callback - called when joining completes or fails
static void joiner_callback(otError aError, void *aContext)
{
    g_joiner_active = false;
    
    if (aError == OT_ERROR_NONE) {
        ESP_LOGI(TAG, "✅ Join SUCCESS! Device is now part of the Thread network");
        
        if (g_event_callback) {
            g_event_callback("join_success", NULL);
        }
        
        // Start Thread interface
        otInstance *instance = esp_openthread_get_instance();
        if (instance) {
            esp_openthread_lock_acquire(portMAX_DELAY);
            otError error = otThreadSetEnabled(instance, true);
            esp_openthread_lock_release();
            
            if (error == OT_ERROR_NONE) {
                ESP_LOGI(TAG, "✅ Thread interface started");
                // LED will be updated by role monitor task
            } else {
                ESP_LOGW(TAG, "Failed to start Thread interface: %d", error);
            }
        }
    } else {
        ESP_LOGE(TAG, "❌ Join FAILED with error: %d", aError);
        
        // Update LED to show detached state
        led_status_set_thread_role("detached");
        
        const char *error_str = "unknown";
        switch (aError) {
            case OT_ERROR_NOT_FOUND:
                error_str = "Commissioner not found";
                break;
            case OT_ERROR_SECURITY:
                error_str = "Security check failed (wrong PSKd?)";
                break;
            case OT_ERROR_RESPONSE_TIMEOUT:
                error_str = "Response timeout";
                break;
            default:
                error_str = "Join failed";
                break;
        }
        
        ESP_LOGE(TAG, "Error details: %s", error_str);
        
        if (g_event_callback) {
            g_event_callback("join_failed", error_str);
        }
    }
}

static void openthread_task(void *arg)
{
    ESP_LOGI(TAG, "OpenThread task started");
    
    // Launch OpenThread mainloop (blocks forever, processing OpenThread events)
    esp_openthread_launch_mainloop();
    
    // Should never reach here
    vTaskDelete(NULL);
}

// Function to log all IPv6 addresses seen by the device
static void log_all_ipv6_addresses(void)
{
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        ESP_LOGW(TAG, "Cannot log IPv6 addresses: OpenThread instance not available");
        return;
    }
    
    esp_openthread_lock_acquire(portMAX_DELAY);
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "📡 IPv6 Addresses visible from device:");
    ESP_LOGI(TAG, "========================================");
    
    // Get all unicast addresses assigned to this device
    const otNetifAddress *addr = otIp6GetUnicastAddresses(instance);
    int addr_count = 0;
    
    while (addr) {
        char addr_str[40];
        snprintf(addr_str, sizeof(addr_str),
                 "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x",
                 (addr->mAddress.mFields.m16[0] >> 8) | ((addr->mAddress.mFields.m16[0] & 0xff) << 8),
                 (addr->mAddress.mFields.m16[1] >> 8) | ((addr->mAddress.mFields.m16[1] & 0xff) << 8),
                 (addr->mAddress.mFields.m16[2] >> 8) | ((addr->mAddress.mFields.m16[2] & 0xff) << 8),
                 (addr->mAddress.mFields.m16[3] >> 8) | ((addr->mAddress.mFields.m16[3] & 0xff) << 8),
                 (addr->mAddress.mFields.m16[4] >> 8) | ((addr->mAddress.mFields.m16[4] & 0xff) << 8),
                 (addr->mAddress.mFields.m16[5] >> 8) | ((addr->mAddress.mFields.m16[5] & 0xff) << 8),
                 (addr->mAddress.mFields.m16[6] >> 8) | ((addr->mAddress.mFields.m16[6] & 0xff) << 8),
                 (addr->mAddress.mFields.m16[7] >> 8) | ((addr->mAddress.mFields.m16[7] & 0xff) << 8));
        
        const char *type = "Unknown";
        if ((addr->mAddress.mFields.m8[0] & 0xfe) == 0xfc) {
            type = "ULA (Unique Local)";
        } else if (addr->mAddress.mFields.m8[0] == 0xfe && (addr->mAddress.mFields.m8[1] & 0xc0) == 0x80) {
            type = "Link-Local";
        } else if ((addr->mAddress.mFields.m8[0] & 0xe0) == 0x20) {
            type = "Global Unicast";
        }
        
        ESP_LOGI(TAG, "  [%d] %s", addr_count, addr_str);
        ESP_LOGI(TAG, "      Type: %s, Prefix: /%d", type, addr->mPrefixLength);
        
        addr_count++;
        addr = addr->mNext;
    }
    
    if (addr_count == 0) {
        ESP_LOGW(TAG, "  No IPv6 addresses found!");
    }
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Total addresses: %d", addr_count);
    ESP_LOGI(TAG, "========================================");
    
    esp_openthread_lock_release();
}

// Task to monitor OpenThread role changes and update LED
static void thread_state_monitor_task(void *arg)
{
    otInstance *instance = esp_openthread_get_instance();
    otDeviceRole previous_role = OT_DEVICE_ROLE_DISABLED;
    static bool ipv6_logged = false;  // Flag to log IPv6 addresses only once
    
    ESP_LOGI(TAG, "Thread state monitor started");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Check every second
        
        if (!instance) {
            instance = esp_openthread_get_instance();
            continue;
        }
        
        esp_openthread_lock_acquire(portMAX_DELAY);
        otDeviceRole current_role = otThreadGetDeviceRole(instance);
        esp_openthread_lock_release();
        
        // Update LED only when role changes
        if (current_role != previous_role) {
            previous_role = current_role;
            
            switch (current_role) {
                case OT_DEVICE_ROLE_DISABLED:
                    ESP_LOGI(TAG, "Thread Role: DISABLED");
                    led_status_set_thread_role("disabled");
                    ipv6_logged = false;  // Reset flag when disconnected
                    break;
                    
                case OT_DEVICE_ROLE_DETACHED:
                    ESP_LOGI(TAG, "Thread Role: DETACHED (not connected)");
                    led_status_set_thread_role("detached");
                    ipv6_logged = false;  // Reset flag when detached
                    
                    // Clear attached bit when detached
                    if (s_thread_event_group) {
                        xEventGroupClearBits(s_thread_event_group, THREAD_ATTACHED_BIT);
                    }
                    break;
                    
                case OT_DEVICE_ROLE_CHILD:
                    ESP_LOGI(TAG, "🟢 Thread Role: CHILD (connected!)");
                    led_status_set_thread_role("child");
                    
                    // Signal that Thread network is attached
                    if (s_thread_event_group) {
                        xEventGroupSetBits(s_thread_event_group, THREAD_ATTACHED_BIT);
                    }
                    
                    // Log IPv6 addresses when first becoming CHILD
                    if (!ipv6_logged) {
                        ESP_LOGI(TAG, "Device joined as CHILD, enumerating IPv6 addresses...");
                        vTaskDelay(pdMS_TO_TICKS(2000));  // Wait 2s for addresses to stabilize
                        log_all_ipv6_addresses();
                        ipv6_logged = true;
                    }
                    break;
                    
                case OT_DEVICE_ROLE_ROUTER:
                    ESP_LOGI(TAG, "🟡 Thread Role: ROUTER");
                    led_status_set_thread_role("router");
                    break;
                    
                case OT_DEVICE_ROLE_LEADER:
                    ESP_LOGI(TAG, "🔵 Thread Role: LEADER");
                    led_status_set_thread_role("leader");
                    break;
                    
                default:
                    ESP_LOGW(TAG, "Thread Role: UNKNOWN (%d)", current_role);
                    break;
            }
        }
    }
}

int thread_joiner_init(void)
{
    ESP_LOGI(TAG, "Initializing Thread Joiner");
    
    // Create event group for Thread network events
    if (!s_thread_event_group) {
        s_thread_event_group = xEventGroupCreate();
        if (!s_thread_event_group) {
            ESP_LOGE(TAG, "Failed to create event group");
            return -1;
        }
    }
    
    // Register eventfd for OpenThread task queue (REQUIRED!)
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 3,  // netif + ot task queue + radio driver
    };
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));
    
    // Initialize OpenThread platform with manual config (ESP-IDF 5.4.1 doesn't have DEFAULT macros)
    esp_openthread_platform_config_t config = {
        .radio_config = {
            .radio_mode = RADIO_MODE_NATIVE,
        },
        .host_config = {
            .host_connection_mode = HOST_CONNECTION_MODE_NONE,
        },
        .port_config = {
            .storage_partition_name = "nvs",
            .netif_queue_size = 10,
            .task_queue_size = 10,
        },
    };
    
    esp_err_t err = esp_openthread_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OpenThread init failed: %s", esp_err_to_name(err));
        return -1;
    }
    
    // Set log level
    otLoggingSetLevel(CONFIG_LOG_DEFAULT_LEVEL);
    
    // Initialize OpenThread network interface
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_OPENTHREAD();
    s_openthread_netif = esp_netif_new(&netif_config);
    assert(s_openthread_netif != NULL);
    ESP_ERROR_CHECK(esp_netif_attach(s_openthread_netif, esp_openthread_netif_glue_init(&config)));
    esp_netif_set_default_netif(s_openthread_netif);
    
    // Create OpenThread task BEFORE mainloop (mainloop is blocking)
    xTaskCreate(openthread_task, "ot_main", 8192, NULL, 5, NULL);
    
    // Create Thread state monitor task to update LED based on role changes
    xTaskCreate(thread_state_monitor_task, "ot_monitor", 4096, NULL, 4, NULL);
    
    // Wait a bit for OpenThread task to initialize
    vTaskDelay(pdMS_TO_TICKS(500));
    
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        ESP_LOGE(TAG, "Failed to get OpenThread instance");
        return -1;
    }
    
    // CRITICAL: Acquire lock before ANY OpenThread API calls
    esp_openthread_lock_acquire(portMAX_DELAY);
    
    // Configure Thread network parameters from Kconfig
    ESP_LOGI(TAG, "Configuring Thread network parameters...");
    otThreadSetNetworkName(instance, CONFIG_OPENTHREAD_NETWORK_NAME);
    otLinkSetPanId(instance, CONFIG_OPENTHREAD_NETWORK_PANID);
    otLinkSetChannel(instance, CONFIG_OPENTHREAD_NETWORK_CHANNEL);
    
    // Set Extended PAN ID (1111111122222222)
    otExtendedPanId extPanId;
    const char *extpan_str = CONFIG_OPENTHREAD_NETWORK_EXTPANID;
    for (int i = 0; i < 8; i++) {
        sscanf(&extpan_str[i*2], "%2hhx", &extPanId.m8[i]);
    }
    otThreadSetExtendedPanId(instance, &extPanId);
    
    // Set Mesh Local Prefix from your Thread Border Router
    // fd3c:12da:fb6a:d420::/64
    otMeshLocalPrefix meshLocalPrefix;
    meshLocalPrefix.m8[0] = 0xfd;
    meshLocalPrefix.m8[1] = 0x3c;
    meshLocalPrefix.m8[2] = 0x12;
    meshLocalPrefix.m8[3] = 0xda;
    meshLocalPrefix.m8[4] = 0xfb;
    meshLocalPrefix.m8[5] = 0x6a;
    meshLocalPrefix.m8[6] = 0xd4;
    meshLocalPrefix.m8[7] = 0x20;
    otThreadSetMeshLocalPrefix(instance, &meshLocalPrefix);
    
    // Set Network Key (Master Key) - TEMPORARY DEFAULT KEY
    // ⚠️ IMPORTANT: You MUST replace this with your actual network key from:
    //    ot-ctl networkkey  (or)  ot-ctl dataset active
    // Using OpenThread default key as fallback (may not work with your network!)
    otNetworkKey networkKey;
    // OpenThread default network key: 00112233445566778899aabbccddeeff
    const char *network_key_str = "00112233445566778899aabbccddeeff";
    
    ESP_LOGW(TAG, "⚠️  Using DEFAULT Network Key - may not match your network!");
    ESP_LOGW(TAG, "⚠️  Get real key with: ot-ctl networkkey");
    
    for (int i = 0; i < 16; i++) {
        sscanf(&network_key_str[i*2], "%2hhx", &networkKey.m8[i]);
    }
    otThreadSetNetworkKey(instance, &networkKey);
    
    ESP_LOGI(TAG, "Network Key set (first 4 bytes): %02x%02x%02x%02x...", 
             networkKey.m8[0], networkKey.m8[1], networkKey.m8[2], networkKey.m8[3]);
    
    // 🔒 MTD Configuration - This device is configured as Minimal Thread Device (End Device only)
    ESP_LOGI(TAG, "⚠️  Device configured as MTD (End Device) - cannot become Router/Leader");
    
    // Set link mode for MTD (simplified - no mSecureDataRequests in MTD)
    otLinkModeConfig linkMode;
    linkMode.mRxOnWhenIdle = true;        // Stay awake (MED - Minimal End Device with RxOnWhenIdle)
    linkMode.mDeviceType = false;         // false = End Device (MTD cannot be Router/Leader)
    linkMode.mNetworkData = true;         // Receive full network data
    otThreadSetLinkMode(instance, linkMode);
    
    ESP_LOGI(TAG, "Link Mode: RxOnWhenIdle=%d, DeviceType=%s, NetworkData=%d",
             linkMode.mRxOnWhenIdle,
             linkMode.mDeviceType ? "FTD/Router" : "MTD/End Device",
             linkMode.mNetworkData);
    
    // Enable IPv6 and Thread
    otIp6SetEnabled(instance, true);
    otThreadSetEnabled(instance, true);  // Enable Thread protocol
    
    // Disable DUA (Domain Unicast Address) to avoid "Failed to perform next registration" warnings
    // This device is an end device, not a border router
    otThreadSetDomainName(instance, "");  // Clear domain name to disable DUA
    
    // Release lock after configuration
    esp_openthread_lock_release();
    
    ESP_LOGI(TAG, "✅ Thread platform initialized successfully");
    ESP_LOGI(TAG, "Network: %s, PAN: 0x%04x, Channel: %d", 
             CONFIG_OPENTHREAD_NETWORK_NAME,
             CONFIG_OPENTHREAD_NETWORK_PANID,
             CONFIG_OPENTHREAD_NETWORK_CHANNEL);
    
    return 0;
}

int thread_joiner_start(const char *pskd, const char *provisioning_url)
{
    if (!pskd || strlen(pskd) < 6) {
        ESP_LOGE(TAG, "Invalid PSKd (must be at least 6 characters)");
        return -1;
    }
    
    ESP_LOGI(TAG, "Starting Joiner with PSKd: %s", pskd);
    
    // Show joining status on LED
    led_status_set_mode(LED_MODE_THREAD_JOINING);
    
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        ESP_LOGE(TAG, "OpenThread instance not available");
        return -1;
    }
    
    esp_openthread_lock_acquire(portMAX_DELAY);
    
    // Start joiner
    otError error = otJoinerStart(instance, 
                                  pskd, 
                                  provisioning_url,  // Can be NULL
                                  "ESP32C6",         // Vendor name
                                  "LwM2M-Client",    // Vendor model
                                  NULL,              // Vendor SW version
                                  NULL,              // Vendor data
                                  joiner_callback,
                                  NULL);             // Context
    
    esp_openthread_lock_release();
    
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to start joiner: %d", error);
        led_status_set_thread_role("detached");
        return -1;
    }
    
    g_joiner_active = true;
    ESP_LOGI(TAG, "📱 Joiner started, waiting for Commissioner response...");
    ESP_LOGI(TAG, "⏳ This may take 10-30 seconds");
    ESP_LOGI(TAG, "💡 LED: Cyan fast blink = joining in progress");
    
    return 0;
}

int thread_joiner_stop(void)
{
    ESP_LOGI(TAG, "Stopping Joiner...");
    
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        return -1;
    }
    
    esp_openthread_lock_acquire(portMAX_DELAY);
    otJoinerStop(instance);
    esp_openthread_lock_release();
    
    g_joiner_active = false;
    ESP_LOGI(TAG, "Joiner stopped");
    return 0;
}

bool thread_joiner_is_active(void)
{
    return g_joiner_active;
}

bool thread_joiner_is_attached(void)
{
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        return false;
    }
    
    esp_openthread_lock_acquire(portMAX_DELAY);
    otDeviceRole role = otThreadGetDeviceRole(instance);
    esp_openthread_lock_release();
    
    return (role == OT_DEVICE_ROLE_CHILD || 
            role == OT_DEVICE_ROLE_ROUTER || 
            role == OT_DEVICE_ROLE_LEADER);
}

const char* thread_joiner_get_role(void)
{
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        return "unavailable";
    }
    
    esp_openthread_lock_acquire(portMAX_DELAY);
    otDeviceRole role = otThreadGetDeviceRole(instance);
    esp_openthread_lock_release();
    
    switch (role) {
        case OT_DEVICE_ROLE_DISABLED:
            return "disabled";
        case OT_DEVICE_ROLE_DETACHED:
            return "detached";
        case OT_DEVICE_ROLE_CHILD:
            return "child";
        case OT_DEVICE_ROLE_ROUTER:
            return "router";
        case OT_DEVICE_ROLE_LEADER:
            return "leader";
        default:
            return "unknown";
    }
}

// Get potential Border Router/Leader IPv6 addresses for server discovery
int thread_joiner_get_border_router_candidates(char addresses[][46], int max_addresses)
{
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        ESP_LOGW(TAG, "OpenThread instance not available for BR discovery");
        return 0;
    }
    
    int count = 0;
    esp_openthread_lock_acquire(portMAX_DELAY);
    
    ESP_LOGI(TAG, "🔍 Searching for Border Router/Leader addresses...");
    
    // Strategy 1: Look for addresses with fd11:22:: prefix (common BR prefix)
    const otNetifAddress *addr = otIp6GetUnicastAddresses(instance);
    while (addr && count < max_addresses) {
        const otIp6Address *ip6 = &addr->mAddress;
        
        // Check for fd11:22:: prefix
        if (ip6->mFields.m8[0] == 0xfd && 
            ip6->mFields.m8[1] == 0x11 &&
            ip6->mFields.m8[2] == 0x00 &&
            ip6->mFields.m8[3] == 0x22) {
            
            // Found a fd11:22:: address - try ::1 (typical BR address)
            snprintf(addresses[count], 46, "fd11:22::1");
            ESP_LOGI(TAG, "  ✓ Candidate[%d]: %s (fd11:22:: prefix gateway)", count, addresses[count]);
            count++;
        }
        addr = addr->mNext;
    }
    
    // Strategy 2: Try common Border Router address patterns
    // Note: MTD devices don't have access to otThreadGetLeaderRloc16()
    // so we use known common patterns instead
    if (count < max_addresses) {
        // Many Thread networks use fd11:22::1 as BR address
        bool already_added = false;
        for (int i = 0; i < count; i++) {
            if (strcmp(addresses[i], "fd11:22::1") == 0) {
                already_added = true;
                break;
            }
        }
        if (!already_added) {
            snprintf(addresses[count], 46, "fd11:22::1");
            ESP_LOGI(TAG, "  ✓ Candidate[%d]: %s (common BR address)", count, addresses[count]);
            count++;
        }
    }
    
    esp_openthread_lock_release();
    
    if (count == 0) {
        ESP_LOGW(TAG, "❌ No Border Router candidates found");
    } else {
        ESP_LOGI(TAG, "✅ Found %d Border Router candidate(s)", count);
    }
    
    return count;
}

EventGroupHandle_t thread_joiner_get_event_group(void)
{
    return s_thread_event_group;
}

void thread_joiner_register_callback(thread_joiner_event_cb_t callback)
{
    g_event_callback = callback;
}

#endif // CONFIG_OPENTHREAD_ENABLED
