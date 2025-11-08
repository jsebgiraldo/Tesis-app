/**
 * LwM2M Client for OpenThread using Anjay (AVSystem)
 * 
 * Este cliente se conecta a un servidor LwM2M a través de la red Thread
 * y expone objetos IPSO como Temperature (3303) y Device (3).
 */

#include "lwm2m_client.h"
#include "temp_object.h"
#include "device_object.h"
#include "sdkconfig.h"

#include <anjay/anjay.h>
#include <anjay/security.h>
#include <anjay/server.h>
#include <avsystem/commons/avs_time.h>
#include <avsystem/commons/avs_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <string.h>

static const char *TAG = "lwm2m_client";

// Global state
static anjay_t *g_anjay = NULL;
static TaskHandle_t g_lwm2m_task = NULL;
static const anjay_dm_object_def_t **g_dev_obj = NULL;
static char g_endpoint_name[64] = {0};
static char g_server_uri[128] = {0};
static bool g_client_running = false;

// Endpoint name from MAC
static void resolve_endpoint_name(void) {
    if (g_endpoint_name[0] != '\0') {
        return; // Already set
    }
    
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(g_endpoint_name, sizeof(g_endpoint_name), 
             "esp32c6-ot-%02x%02x%02x", mac[3], mac[4], mac[5]);
    
    ESP_LOGI(TAG, "LwM2M Endpoint: %s", g_endpoint_name);
}

// Setup Security Object
static int setup_security(anjay_t *anjay, const char *server_uri) {
    if (!anjay || !server_uri) {
        return -1;
    }
    
    anjay_security_object_purge(anjay);
    
    const anjay_security_instance_t sec = {
        .ssid = 1,
        .server_uri = server_uri,
        .security_mode = ANJAY_SECURITY_NOSEC, // No DTLS for now
        .bootstrap_server = false,
    };
    
    anjay_iid_t sec_iid = ANJAY_ID_INVALID;
    int result = anjay_security_object_add_instance(anjay, &sec, &sec_iid);
    if (result) {
        ESP_LOGE(TAG, "Failed to add Security instance: %d", result);
        return result;
    }
    
    ESP_LOGI(TAG, "Security configured: %s", server_uri);
    return 0;
}

// Setup Server Object
static int setup_server(anjay_t *anjay) {
    if (!anjay) {
        return -1;
    }
    
    anjay_server_object_purge(anjay);
    
    anjay_server_instance_t srv = {
        .ssid = 1,
        .lifetime = 300, // 5 minutes
        .default_min_period = 1,
        .default_max_period = 60,
        .disable_timeout = -1,
        .binding = "U", // UDP binding
    };
    
    anjay_iid_t srv_iid = ANJAY_ID_INVALID;
    int result = anjay_server_object_add_instance(anjay, &srv, &srv_iid);
    if (result) {
        ESP_LOGE(TAG, "Failed to add Server instance: %d", result);
        return result;
    }
    
    ESP_LOGI(TAG, "Server configured with lifetime=%d", srv.lifetime);
    return 0;
}

// Main LwM2M task
static void lwm2m_client_task(void *arg) {
    (void) arg;
    
    // Set log level
    avs_log_set_default_level(AVS_LOG_DEBUG);
    
    // Ensure endpoint name is set
    resolve_endpoint_name();
    
    // Create Anjay instance
    anjay_configuration_t cfg = {
        .endpoint_name = g_endpoint_name,
        .in_buffer_size = 4000,
        .out_buffer_size = 4000,
        .msg_cache_size = 4000,
    };
    
    anjay_t *anjay = anjay_new(&cfg);
    if (!anjay) {
        ESP_LOGE(TAG, "Could not create Anjay instance");
        vTaskDelete(NULL);
        return;
    }
    
    g_anjay = anjay;
    
    // Install Security and Server objects
    if (anjay_security_object_install(anjay) || anjay_server_object_install(anjay)) {
        ESP_LOGE(TAG, "Could not install Security/Server objects");
        goto cleanup;
    }
    
    // Wait for auto-configuration
    ESP_LOGI(TAG, "Waiting for auto-configuration...");
    while (g_server_uri[0] == '\0' && g_client_running) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    if (!g_client_running) {
        goto cleanup;
    }
    
    // Setup security and server
    if (setup_security(anjay, g_server_uri) || setup_server(anjay)) {
        goto cleanup;
    }
    
    // Register Device Object (3)
    g_dev_obj = device_object_create(g_endpoint_name);
    if (!g_dev_obj || anjay_register_object(anjay, g_dev_obj)) {
        ESP_LOGE(TAG, "Could not register Device (3) object");
        goto cleanup;
    }
    
    // Register Temperature Object (3303)
    if (anjay_register_object(anjay, temp_object_def())) {
        ESP_LOGE(TAG, "Could not register Temperature (3303) object");
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "Starting LwM2M event loop...");
    
    // Main event loop
    const avs_time_duration_t max_wait = avs_time_duration_from_scalar(100, AVS_TIME_MS);
    uint32_t update_counter = 0;
    
    while (g_client_running) {
        anjay_event_loop_run(anjay, max_wait);
        
        // Update temperature every 10 seconds
        if (++update_counter >= 100) { // 100 * 100ms = 10s
            update_counter = 0;
            temp_object_update(anjay);
            device_object_update(anjay, g_dev_obj);
        }
    }
    
cleanup:
    ESP_LOGI(TAG, "Cleaning up LwM2M client...");
    
    device_object_release(g_dev_obj);
    g_dev_obj = NULL;
    
    anjay_delete(anjay);
    g_anjay = NULL;
    g_lwm2m_task = NULL;
    
    vTaskDelete(NULL);
}

// Public API implementations

esp_err_t lwm2m_client_init(void) {
    resolve_endpoint_name();
    ESP_LOGI(TAG, "LwM2M client initialized");
    return ESP_OK;
}

esp_err_t lwm2m_client_start(void) {
    if (g_lwm2m_task != NULL) {
        ESP_LOGW(TAG, "LwM2M client already running");
        return ESP_ERR_INVALID_STATE;
    }
    
    g_client_running = true;
    
    BaseType_t result = xTaskCreate(lwm2m_client_task, "lwm2m", 
                                    8192, NULL, tskIDLE_PRIORITY + 2, 
                                    &g_lwm2m_task);
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LwM2M task");
        g_client_running = false;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "LwM2M client task started");
    return ESP_OK;
}

esp_err_t lwm2m_client_stop(void) {
    if (g_lwm2m_task == NULL) {
        ESP_LOGW(TAG, "LwM2M client not running");
        return ESP_ERR_INVALID_STATE;
    }
    
    g_client_running = false;
    ESP_LOGI(TAG, "Stopping LwM2M client...");
    
    // Wait for task to finish
    int timeout = 50; // 5 seconds
    while (g_lwm2m_task != NULL && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    if (g_lwm2m_task != NULL) {
        ESP_LOGW(TAG, "LwM2M task did not stop gracefully");
    }
    
    g_server_uri[0] = '\0';
    return ESP_OK;
}

bool lwm2m_client_is_registered(void) {
    return (g_anjay != NULL && g_client_running);
}

const char* lwm2m_client_get_state_str(void) {
    if (!g_client_running) {
        return "STOPPED";
    }
    if (g_anjay == NULL) {
        return "INITIALIZING";
    }
    if (g_server_uri[0] == '\0') {
        return "WAITING_CONFIG";
    }
    return "RUNNING";
}

esp_err_t lwm2m_client_auto_configure(const discovered_service_t *service) {
    if (!service) {
        ESP_LOGE(TAG, "Invalid service pointer");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Build CoAP URI: coap://[ipv6]:port
    snprintf(g_server_uri, sizeof(g_server_uri), 
             "coap://[%s]:%u", service->ipv6_addr, service->port);
    
    ESP_LOGI(TAG, "Auto-configured server: %s", g_server_uri);
    return ESP_OK;
}

esp_err_t lwm2m_client_update_temperature(float temperature) {
    if (!g_anjay || !g_client_running) {
        ESP_LOGW(TAG, "Client not running");
        return ESP_ERR_INVALID_STATE;
    }
    
    temp_object_set_value(temperature);
    temp_object_update(g_anjay);
    
    ESP_LOGI(TAG, "Temperature updated to %.1f°C", temperature);
    return ESP_OK;
}
