/* OpenThread CLI Example for ESP32-C6 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_openthread.h"
#include "esp_openthread_cli.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"
#include "esp_ot_config.h"
#include "esp_vfs_eventfd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "openthread/instance.h"
#include "openthread/logging.h"
#include "openthread/tasklet.h"
#include "openthread/dataset.h"
#include "openthread/thread.h"
#include "openthread/ip6.h"
#include "openthread/cli.h"
#include "sdkconfig.h"
#include "ot_custom_commands.h"
#include "ot_auto_discovery.h"
#include "lwm2m_client.h"

#define TAG "ot_esp32c6"

static void ot_task_worker(void *aContext)
{
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };

    ESP_ERROR_CHECK(esp_openthread_init(&config));
    
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_OPENTHREAD();
    esp_netif_t *openthread_netif = esp_netif_new(&cfg);
    assert(openthread_netif != NULL);

    ESP_ERROR_CHECK(esp_netif_attach(openthread_netif, esp_openthread_netif_glue_init(&config)));

    esp_openthread_cli_init();

    // Initialize custom commands
    ot_custom_commands_init();

    esp_openthread_lock_acquire(portMAX_DELAY);
    (void)otLoggingSetLevel(OT_LOG_LEVEL_WARN);  // Only show warnings and errors
    esp_openthread_lock_release();
    
    ESP_LOGI(TAG, "OpenThread initialized");

    // Initialize auto-discovery system
    auto_discovery_config_t auto_config = {
        .auto_join_enabled = true,
        .auto_discover_services = true,
        .ping_discovered_services = true,
        .scan_timeout_ms = 30000,       // 30 seconds
        .join_timeout_ms = 60000,       // 60 seconds
        .discovery_timeout_ms = 20000,  // 20 seconds
        .min_rssi_threshold = -80,      // Minimum signal strength
        
        // Border Router network configuration
        .network_name = "OpenThreadDemo",
        .panid = 0x1234,
        .channel = 15,
        .ext_panid = 0x1111111122222222ULL,
        .mesh_prefix = "fdca:6fb:455f:9103::",
        .network_key = {
            // Tu network key real del Border Router
            0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
            0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
        }
    };
    
    // Join existing network using CLI commands (prevents forming new partition)
    ESP_LOGI(TAG, "Joining OpenThreadDemo network...");
    ESP_LOGI(TAG, "Network: %s, PAN ID: 0x%04x, Channel: %d", 
             auto_config.network_name, auto_config.panid, auto_config.channel);
    
    // Wait for OpenThread to initialize
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Configure dataset using CLI commands to ensure proper join behavior
    // IMPORTANT: Commands must be in MUTABLE buffers (otCliInputLine modifies them)
    char cmd_clear[] = "dataset clear";
    char cmd_commit[] = "dataset commit active";
    char cmd_ifup[] = "ifconfig up";
    char cmd_start[] = "thread start";
    char cmd_key[] = "dataset networkkey 00112233445566778899aabbccddeeff";
    char cmd[128];
    
    // Clear any existing dataset
    otCliInputLine(cmd_clear);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Configure network parameters
    snprintf(cmd, sizeof(cmd), "dataset networkname %s", auto_config.network_name);
    otCliInputLine(cmd);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    snprintf(cmd, sizeof(cmd), "dataset panid 0x%04x", auto_config.panid);
    otCliInputLine(cmd);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    snprintf(cmd, sizeof(cmd), "dataset channel %d", auto_config.channel);
    otCliInputLine(cmd);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    snprintf(cmd, sizeof(cmd), "dataset extpanid %016llx", auto_config.ext_panid);
    otCliInputLine(cmd);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Set network key
    otCliInputLine(cmd_key);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Commit dataset
    otCliInputLine(cmd_commit);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Dataset configured, starting interface...");
    
    // Enable interface
    otCliInputLine(cmd_ifup);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Start Thread protocol - this will SCAN and JOIN existing network
    ESP_LOGI(TAG, "Starting Thread - will scan for existing network on channel %d...", auto_config.channel);
    otCliInputLine(cmd_start);
    
    ESP_LOGI(TAG, "Thread started - scanning for network OpenThreadDemo on channel %d", auto_config.channel);
    ESP_LOGI(TAG, "Use 'state' command to check role once attached");
    
    // Wait for network attachment before starting service discovery
    ESP_LOGI(TAG, "Waiting 10 seconds for network attachment...");
    vTaskDelay(pdMS_TO_TICKS(10000));
    
        // Initialize auto-discovery for LwM2M service discovery
    ESP_LOGI(TAG, "Starting service discovery...");
    esp_err_t ret = ot_auto_discovery_init(&auto_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Auto-discovery initialized for service discovery");
        ot_auto_discovery_start();
    } else {
        ESP_LOGE(TAG, "Failed to initialize auto-discovery: %s", esp_err_to_name(ret));
    }
    
    // Initialize LwM2M client
    ret = lwm2m_client_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "LwM2M client initialized");
    } else {
        ESP_LOGE(TAG, "Failed to initialize LwM2M client: %s", esp_err_to_name(ret));
    }

    esp_openthread_cli_create_task();

    esp_openthread_launch_mainloop();

    esp_openthread_netif_glue_deinit();
    esp_netif_destroy(openthread_netif);
    esp_vfs_eventfd_unregister();
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 3,
    };
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));

    xTaskCreate(ot_task_worker, "ot_task", 10240, NULL, 5, NULL);
}