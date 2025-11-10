/* Improved OpenThread + Anjay Integration
   Modern implementation using Dataset API with optimizations
*/
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <sys/param.h>
#include <esp_http_server.h>
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_eth.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_check.h"
#include <nvs_flash.h>

#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"
#include "esp_openthread_cli.h"
#include "esp_ot_config.h"
#include "esp_vfs_eventfd.h"

#include "openthread/error.h"
#include "openthread/logging.h"
#include "openthread/tasklet.h"
#include "openthread/instance.h"
#include "openthread/cli.h"
#include "openthread/coap_secure.h"
#include "openthread/thread.h"
#include "openthread/dataset.h"  // Modern Dataset API
#include "openthread/ip6.h"

#include "anjay_config.h"
#include "utils.h"
#include "esp_ot_cli_extension.h"

#define CONFIG_ESP_WIFI_SSID      "SEBAS_LAN_AP"
#define CONFIG_ESP_WIFI_PASSWORD  "1053866507"
#define CONFIG_ESP_MAXIMUM_RETRY  5

static EventGroupHandle_t event_group;
#define EVENT_SLEEP_MODE_ON (1 << 0)

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;

static const char *TAG = "MAIN";

// ===== Modern OpenThread Configuration =====

// Thread network parameters - CENTRALIZED CONFIGURATION
typedef struct {
    const char *network_name;
    uint16_t panid;
    uint8_t channel;
    uint64_t ext_panid;
    const char *mesh_prefix;
    uint8_t network_key[OT_NETWORK_KEY_SIZE];
} thread_network_config_t;

static const thread_network_config_t thread_config = {
    .network_name = CONFIG_OPENTHREAD_NETWORK_NAME,
    .panid = CONFIG_OPENTHREAD_NETWORK_PANID,
    .channel = CONFIG_OPENTHREAD_NETWORK_CHANNEL,
    .ext_panid = 0x1111111122222222ULL,  // Configure from CONFIG_OPENTHREAD_NETWORK_EXTPANID
    .mesh_prefix = "fdca:6fb:455f:9103::",
    .network_key = {
        // Configure from CONFIG_OPENTHREAD_NETWORK_PSKC
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    }
};

static void log_handler(avs_log_level_t level, const char *module, const char *msg) {
    esp_log_level_t esp_level = ESP_LOG_NONE;
    switch (level) {
    case AVS_LOG_QUIET:
        esp_level = ESP_LOG_NONE;
        break;
    case AVS_LOG_ERROR:
        esp_level = ESP_LOG_ERROR;
        break;
    case AVS_LOG_WARNING:
        esp_level = ESP_LOG_WARN;
        break;
    case AVS_LOG_INFO:
        esp_level = ESP_LOG_INFO;
        break;
    case AVS_LOG_DEBUG:
        esp_level = ESP_LOG_DEBUG;
        break;
    case AVS_LOG_TRACE:
        esp_level = ESP_LOG_VERBOSE;
        break;
    }
    ESP_LOG_LEVEL_LOCAL(esp_level, "anjay", "%s", msg);
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == ETH_EVENT) {
        if (event_id == ETHERNET_EVENT_START) return;
        if (event_id == ETHERNET_EVENT_STOP) return;
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void register_wifi(void)
{
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
         .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

static esp_netif_t *init_openthread_netif(const esp_openthread_platform_config_t *config)
{
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_OPENTHREAD();
    esp_netif_t *netif = esp_netif_new(&cfg);
    assert(netif != NULL);
    ESP_ERROR_CHECK(esp_netif_attach(netif, esp_openthread_netif_glue_init(config)));

    return netif;
}

/**
 * @brief Configure Thread network using modern Dataset API
 * 
 * This replaces the deprecated individual setter functions with a single
 * dataset configuration that's atomic and properly handles NVS storage.
 */
static esp_err_t configure_thread_network(otInstance *instance)
{
    esp_openthread_lock_acquire(portMAX_DELAY);
    
    // Check if we already have a valid dataset stored
    otOperationalDataset storedDataset;
    otError error = otDatasetGetActive(instance, &storedDataset);
    
    bool needsConfiguration = true;
    if (error == OT_ERROR_NONE) {
        // We have a stored dataset - check if it matches our target network
        char storedNetworkName[OT_NETWORK_NAME_MAX_SIZE + 1] = {0};
        memcpy(storedNetworkName, storedDataset.mNetworkName.m8, OT_NETWORK_NAME_MAX_SIZE);
        
        if (storedDataset.mPanId == thread_config.panid && 
            storedDataset.mChannel == thread_config.channel &&
            strcmp(storedNetworkName, thread_config.network_name) == 0) {
            ESP_LOGI(TAG, "✓ Valid dataset already stored - using it");
            needsConfiguration = false;
        } else {
            ESP_LOGW(TAG, "Stored dataset doesn't match - reconfiguring");
            ESP_LOGI(TAG, "  Stored: %s, PAN:0x%04x, Ch:%d", 
                     storedNetworkName, storedDataset.mPanId, storedDataset.mChannel);
            ESP_LOGI(TAG, "  Target: %s, PAN:0x%04x, Ch:%d", 
                     thread_config.network_name, thread_config.panid, thread_config.channel);
        }
    } else {
        ESP_LOGI(TAG, "No stored dataset found - configuring from scratch");
    }
    
    if (needsConfiguration) {
        ESP_LOGI(TAG, "Configuring Thread network: %s, PAN:0x%04x, Ch:%d", 
                 thread_config.network_name, thread_config.panid, thread_config.channel);
        
        otOperationalDataset dataset;
        memset(&dataset, 0, sizeof(dataset));
        
        // Network Name
        size_t length = strlen(thread_config.network_name);
        memcpy(dataset.mNetworkName.m8, thread_config.network_name, length);
        dataset.mComponents.mIsNetworkNamePresent = true;
        
        // PAN ID
        dataset.mPanId = thread_config.panid;
        dataset.mComponents.mIsPanIdPresent = true;
        
        // Channel
        dataset.mChannel = thread_config.channel;
        dataset.mComponents.mIsChannelPresent = true;
        
        // Extended PAN ID
        uint64_t extPanId = thread_config.ext_panid;
        for (int i = 0; i < 8; i++) {
            dataset.mExtendedPanId.m8[i] = (extPanId >> (56 - i * 8)) & 0xff;
        }
        dataset.mComponents.mIsExtendedPanIdPresent = true;
        
        // Network Key
        memcpy(dataset.mNetworkKey.m8, thread_config.network_key, OT_NETWORK_KEY_SIZE);
        dataset.mComponents.mIsNetworkKeyPresent = true;
        
        // Mesh-Local Prefix (prevents scanning)
        otIp6Prefix meshLocalPrefix;
        memset(&meshLocalPrefix, 0, sizeof(meshLocalPrefix));
        // Parse "fdca:6fb:455f:9103::/64"
        meshLocalPrefix.mPrefix.mFields.m8[0] = 0xfd;
        meshLocalPrefix.mPrefix.mFields.m8[1] = 0xca;
        meshLocalPrefix.mPrefix.mFields.m8[2] = 0x06;
        meshLocalPrefix.mPrefix.mFields.m8[3] = 0xfb;
        meshLocalPrefix.mPrefix.mFields.m8[4] = 0x45;
        meshLocalPrefix.mPrefix.mFields.m8[5] = 0x5f;
        meshLocalPrefix.mPrefix.mFields.m8[6] = 0x91;
        meshLocalPrefix.mPrefix.mFields.m8[7] = 0x03;
        meshLocalPrefix.mLength = 64;
        memcpy(&dataset.mMeshLocalPrefix, &meshLocalPrefix.mPrefix, sizeof(otIp6Address));
        dataset.mComponents.mIsMeshLocalPrefixPresent = true;
        
        // Channel Mask
        dataset.mChannelMask = 1 << thread_config.channel;
        dataset.mComponents.mIsChannelMaskPresent = true;
        
        // Security Policy
        dataset.mSecurityPolicy.mRotationTime = 672;
        dataset.mSecurityPolicy.mObtainNetworkKeyEnabled = true;
        dataset.mSecurityPolicy.mNativeCommissioningEnabled = true;
        dataset.mSecurityPolicy.mRoutersEnabled = true;
        dataset.mSecurityPolicy.mExternalCommissioningEnabled = true;
        dataset.mComponents.mIsSecurityPolicyPresent = true;
        
        // Active Timestamp
        dataset.mActiveTimestamp.mSeconds = 1;
        dataset.mActiveTimestamp.mTicks = 0;
        dataset.mActiveTimestamp.mAuthoritative = false;
        dataset.mComponents.mIsActiveTimestampPresent = true;
        
        // Set the active dataset
        error = otDatasetSetActive(instance, &dataset);
        if (error != OT_ERROR_NONE) {
            ESP_LOGE(TAG, "Failed to set active dataset: %d", error);
            esp_openthread_lock_release();
            return ESP_FAIL;
        }
        
        ESP_LOGI(TAG, "✓ Dataset configured successfully");
    }
    
    // Configure device as End Device only (never Router/Leader)
    otLinkModeConfig linkMode;
    linkMode.mRxOnWhenIdle = true;   // Always listening
    linkMode.mDeviceType = false;    // End Device (not Router)
    linkMode.mNetworkData = true;    // Request full network data
    error = otThreadSetLinkMode(instance, linkMode);
    if (error != OT_ERROR_NONE) {
        ESP_LOGW(TAG, "Failed to set link mode: %d", error);
    } else {
        ESP_LOGI(TAG, "✓ Configured as End Device (Child only)");
    }
    
    esp_openthread_lock_release();
    return ESP_OK;
}

/**
 * @brief Wait for Thread network attachment with timeout
 * 
 * @return ESP_OK if attached, ESP_ERR_TIMEOUT if timeout
 */
static esp_err_t wait_for_thread_attachment(otInstance *instance, int max_wait_seconds)
{
    const int check_interval_ms = 200;
    int waited_ms = 0;
    otDeviceRole role = OT_DEVICE_ROLE_DISABLED;
    
    ESP_LOGI(TAG, "Waiting for Thread network attachment...");
    
    while (waited_ms < (max_wait_seconds * 1000)) {
        esp_openthread_lock_acquire(portMAX_DELAY);
        role = otThreadGetDeviceRole(instance);
        esp_openthread_lock_release();
        
        if (role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER || role == OT_DEVICE_ROLE_LEADER) {
            const char* role_str = (role == OT_DEVICE_ROLE_CHILD) ? "Child" : 
                                   (role == OT_DEVICE_ROLE_ROUTER) ? "Router" : "Leader";
            ESP_LOGI(TAG, "✓ Attached as %s! (took %.1f seconds)", 
                     role_str, waited_ms / 1000.0);
            return ESP_OK;
        }
        
        vTaskDelay(pdMS_TO_TICKS(check_interval_ms));
        waited_ms += check_interval_ms;
        
        if (waited_ms % 3000 == 0) {
            ESP_LOGI(TAG, "Attaching... (%.1f seconds)", waited_ms / 1000.0);
        }
    }
    
    ESP_LOGE(TAG, "❌ Failed to attach after %d seconds", max_wait_seconds);
    ESP_LOGE(TAG, "Check Border Router is running and network parameters match");
    return ESP_ERR_TIMEOUT;
}

#define TIMER_WAKEUP_TIME_US    (20 * 1000 * 1000)

void trigger_event_task(void *pvParameter) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(20*1000));
        ESP_LOGI(TAG, "Triggering event...");
        xEventGroupSetBits(event_group, EVENT_SLEEP_MODE_ON); 
    }
}

void event_listener_task(void *pvParameter) {
    event_group = xEventGroupCreate();
    if (event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return;
    }

    while (1) {
        ESP_LOGI(TAG, "Waiting for event...");
        xEventGroupWaitBits(
            event_group,
            EVENT_SLEEP_MODE_ON,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY
        );

        ESP_LOGI(TAG, "Event received! Executing task...");

        printf("Entering light sleep\n");
        int64_t t_before_us = esp_timer_get_time();
        esp_light_sleep_start();
        int64_t t_after_us = esp_timer_get_time();

        const char* wakeup_reason;
        switch (esp_sleep_get_wakeup_cause()) {
            case ESP_SLEEP_WAKEUP_TIMER:
                wakeup_reason = "timer";
                break;
            default:
                wakeup_reason = "other";
                break;
        }
        printf("Returned from light sleep, reason: %s, t=%lld ms, slept for %lld ms\n",
               wakeup_reason, t_after_us / 1000, (t_after_us - t_before_us) / 1000);
    
        esp_system_abort("Rebooting ...");
    }
}

static void ot_task_worker(void *aContext)
{
    // Configure eventfd for OpenThread
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 3,  // netif + ot task queue + radio driver
    };
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));

    // Initialize OpenThread platform
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    ESP_ERROR_CHECK(esp_openthread_init(&config));

    // Set logging level
    esp_openthread_lock_acquire(portMAX_DELAY);
    (void)otLoggingSetLevel(OT_LOG_LEVEL_INFO);
    esp_openthread_lock_release();

    // Initialize network interface
    esp_netif_t *openthread_netif = init_openthread_netif(&config);
    esp_netif_set_default_netif(openthread_netif);
    
    ESP_LOGI(TAG, "OpenThread platform initialized");

    // Get OpenThread instance
    otInstance *instance = esp_openthread_get_instance();

    // Configure Thread network using modern Dataset API
    if (configure_thread_network(instance) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure Thread network");
        goto cleanup;
    }

    // Enable IPv6 interface
    esp_openthread_lock_acquire(portMAX_DELAY);
    otError error = otIp6SetEnabled(instance, true);
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to enable IPv6: %d", error);
        esp_openthread_lock_release();
        goto cleanup;
    }

    // Start Thread protocol
    error = otThreadSetEnabled(instance, true);
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to start Thread: %d", error);
        esp_openthread_lock_release();
        goto cleanup;
    }
    esp_openthread_lock_release();

    ESP_LOGI(TAG, "Thread protocol started");

    // Wait for network attachment (non-blocking for mainloop)
    wait_for_thread_attachment(instance, 15);

    // Initialize CLI
    esp_openthread_cli_init();
    esp_cli_custom_command_init();
    esp_openthread_cli_create_task();

    // Launch OpenThread mainloop (blocking)
    esp_openthread_launch_mainloop();

cleanup:
    // Clean up
    esp_openthread_netif_glue_deinit();
    esp_netif_destroy(openthread_netif);
    esp_vfs_eventfd_unregister();
    vTaskDelete(NULL);
}

void app_main(void)
{
    // Initialize NVS
    ESP_ERROR_CHECK(nvs_flash_init());
    
    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Optional: Initialize WiFi
    // register_wifi();

    // Start OpenThread task
    xTaskCreate(ot_task_worker, "ot_main", 10240, NULL, 5, NULL);

    // Configure Anjay logging
    avs_log_set_handler(log_handler);
    avs_log_set_default_level(AVS_LOG_TRACE);

    // Initialize and start Anjay
    anjay_init();
    xTaskCreate(&anjay_task, "anjay_task", 16384, NULL, 5, NULL);
    
    // Optional: Sleep mode tasks
    // esp_sleep_enable_timer_wakeup(TIMER_WAKEUP_TIME_US);
    // xTaskCreate(trigger_event_task, "Trigger Event Task", 2048, NULL, 5, NULL);
    // xTaskCreate(event_listener_task, "Event Listener Task", 2048, NULL, 5, NULL);
}
