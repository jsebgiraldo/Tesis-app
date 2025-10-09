#include "thingsboard_provision.h"
#include "sdkconfig.h"

#include <anjay/anjay.h>
#include <anjay/security.h>
#include <anjay/server.h>

#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <string.h>

static const char *TAG = "tb_provision";

// NVS namespace and keys
#define NVS_NAMESPACE "tb_lwm2m"
#define NVS_KEY_PROVISIONED "provisioned"
#define NVS_KEY_SERVER_URI "server_uri"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PSK_ID "psk_id"
#define NVS_KEY_PSK_KEY "psk_key"
#define NVS_KEY_SECURITY_MODE "sec_mode"

// Provisioning state
typedef struct {
    bool is_provisioned;
    bool bootstrap_finished;
    char server_uri[128];
    anjay_ssid_t server_ssid;
    int security_mode;
    char psk_identity[64];
    char psk_key[128];
} tb_provision_state_t;

static tb_provision_state_t g_provision_state = {
    .is_provisioned = false,
    .bootstrap_finished = false,
    .server_uri = "",
    .server_ssid = 123,  // ThingsBoard LwM2M Server SSID from your JSON config
    .security_mode = 3,  // NoSec by default
    .psk_identity = "",
    .psk_key = ""
};

void thingsboard_provision_init(void) {
    ESP_LOGI(TAG, "Initializing ThingsBoard provisioning");
    
    // Initialize NVS if not already done
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    memset(&g_provision_state, 0, sizeof(g_provision_state));
    g_provision_state.security_mode = 3;  // NoSec default
    g_provision_state.server_ssid = 1;
}

bool thingsboard_is_provisioned(void) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "No provisioning data found in NVS");
        return false;
    }
    
    uint8_t provisioned = 0;
    err = nvs_get_u8(nvs, NVS_KEY_PROVISIONED, &provisioned);
    nvs_close(nvs);
    
    if (err == ESP_OK && provisioned == 1) {
        ESP_LOGI(TAG, "Device is already provisioned");
        g_provision_state.is_provisioned = true;
        return true;
    }
    
    ESP_LOGI(TAG, "Device is NOT provisioned");
    return false;
}

int thingsboard_setup_bootstrap(anjay_t *anjay, const char *endpoint_name) {
#if CONFIG_LWM2M_BOOTSTRAP
    ESP_LOGI(TAG, "Setting up Bootstrap for endpoint: %s", endpoint_name);
    ESP_LOGI(TAG, "Bootstrap URI: %s", CONFIG_LWM2M_BOOTSTRAP_URI);
    
    // Configure Bootstrap Security object (instance 0 for Bootstrap Server)
    const anjay_security_instance_t bootstrap_security = {
        .ssid = ANJAY_SSID_BOOTSTRAP,
        .server_uri = CONFIG_LWM2M_BOOTSTRAP_URI,
        .security_mode = CONFIG_LWM2M_BOOTSTRAP_SECURITY_MODE,
#if CONFIG_LWM2M_BOOTSTRAP_SECURITY_MODE == 0  // PSK
        .public_cert_or_psk_identity = (const uint8_t *)(CONFIG_LWM2M_BOOTSTRAP_PSK_ID[0] ? 
                                        CONFIG_LWM2M_BOOTSTRAP_PSK_ID : endpoint_name),
        .public_cert_or_psk_identity_size = strlen(CONFIG_LWM2M_BOOTSTRAP_PSK_ID[0] ? 
                                            CONFIG_LWM2M_BOOTSTRAP_PSK_ID : endpoint_name),
        .private_cert_or_psk_key = (const uint8_t *)CONFIG_LWM2M_BOOTSTRAP_PSK_KEY,
        .private_cert_or_psk_key_size = strlen(CONFIG_LWM2M_BOOTSTRAP_PSK_KEY),
#endif
#if CONFIG_LWM2M_BOOTSTRAP_SERVER_ACCOUNT_TIMEOUT > 0
        .bootstrap_server_account_timeout = CONFIG_LWM2M_BOOTSTRAP_SERVER_ACCOUNT_TIMEOUT,
#endif
    };
    
    // Purge any existing Security instances
    anjay_security_object_purge(anjay);
    
    // Add Bootstrap Security instance
    anjay_iid_t bootstrap_iid = ANJAY_ID_INVALID;
    if (anjay_security_object_add_instance(anjay, &bootstrap_security, &bootstrap_iid)) {
        ESP_LOGE(TAG, "Failed to add Bootstrap Security instance");
        return -1;
    }
    
    ESP_LOGI(TAG, "Bootstrap Security instance added (iid=%d, ssid=%d)", 
             (int)bootstrap_iid, ANJAY_SSID_BOOTSTRAP);
    
    return 0;
#else
    ESP_LOGW(TAG, "Bootstrap not enabled in configuration");
    return -1;
#endif
}

bool thingsboard_bootstrap_finished(void) {
    return g_provision_state.bootstrap_finished;
}

int thingsboard_get_provisioned_uri(char *uri_buf, size_t buf_size) {
    if (!g_provision_state.is_provisioned || strlen(g_provision_state.server_uri) == 0) {
        return -1;
    }
    
    strlcpy(uri_buf, g_provision_state.server_uri, buf_size);
    return 0;
}

int thingsboard_save_credentials(void) {
    ESP_LOGI(TAG, "Saving provisioned credentials to NVS");
    
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
        return -1;
    }
    
    // Mark as provisioned
    nvs_set_u8(nvs, NVS_KEY_PROVISIONED, 1);
    
    // Save server URI
    if (strlen(g_provision_state.server_uri) > 0) {
        nvs_set_str(nvs, NVS_KEY_SERVER_URI, g_provision_state.server_uri);
    }
    
    // Save SSID
    nvs_set_u16(nvs, NVS_KEY_SSID, g_provision_state.server_ssid);
    
    // Save security mode
    nvs_set_i32(nvs, NVS_KEY_SECURITY_MODE, g_provision_state.security_mode);
    
    // Save PSK credentials if available
    if (strlen(g_provision_state.psk_identity) > 0) {
        nvs_set_str(nvs, NVS_KEY_PSK_ID, g_provision_state.psk_identity);
    }
    if (strlen(g_provision_state.psk_key) > 0) {
        nvs_set_str(nvs, NVS_KEY_PSK_KEY, g_provision_state.psk_key);
    }
    
    err = nvs_commit(nvs);
    nvs_close(nvs);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        return -1;
    }
    
    ESP_LOGI(TAG, "Credentials saved successfully");
    g_provision_state.is_provisioned = true;
    return 0;
}

int thingsboard_load_credentials(anjay_t *anjay) {
    if (!thingsboard_is_provisioned()) {
        ESP_LOGI(TAG, "No stored credentials to load");
        return -1;
    }
    
    ESP_LOGI(TAG, "Loading provisioned credentials from NVS");
    
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for reading");
        return -1;
    }
    
    // Load server URI
    size_t uri_size = sizeof(g_provision_state.server_uri);
    err = nvs_get_str(nvs, NVS_KEY_SERVER_URI, g_provision_state.server_uri, &uri_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load server URI");
        nvs_close(nvs);
        return -1;
    }
    
    // Load SSID
    uint16_t ssid = 1;
    nvs_get_u16(nvs, NVS_KEY_SSID, &ssid);
    g_provision_state.server_ssid = ssid;
    
    // Load security mode
    int32_t sec_mode = 3;
    nvs_get_i32(nvs, NVS_KEY_SECURITY_MODE, &sec_mode);
    g_provision_state.security_mode = sec_mode;
    
    // Load PSK credentials if available
    size_t psk_id_size = sizeof(g_provision_state.psk_identity);
    nvs_get_str(nvs, NVS_KEY_PSK_ID, g_provision_state.psk_identity, &psk_id_size);
    
    size_t psk_key_size = sizeof(g_provision_state.psk_key);
    nvs_get_str(nvs, NVS_KEY_PSK_KEY, g_provision_state.psk_key, &psk_key_size);
    
    nvs_close(nvs);
    
    ESP_LOGI(TAG, "Loaded credentials - URI: %s, SSID: %d, Security Mode: %d",
             g_provision_state.server_uri, 
             g_provision_state.server_ssid,
             g_provision_state.security_mode);
    
    // Now configure Anjay with loaded credentials
    // Add Security object instance for LwM2M Server
    const anjay_security_instance_t security = {
        .ssid = g_provision_state.server_ssid,  // Should be 123 from ThingsBoard
        .server_uri = g_provision_state.server_uri,
        .security_mode = g_provision_state.security_mode,
        .bootstrap_server = false,  // This is NOT the Bootstrap Server
        .public_cert_or_psk_identity = (const uint8_t *)g_provision_state.psk_identity,
        .public_cert_or_psk_identity_size = strlen(g_provision_state.psk_identity),
        .private_cert_or_psk_key = (const uint8_t *)g_provision_state.psk_key,
        .private_cert_or_psk_key_size = strlen(g_provision_state.psk_key)
    };
    
    anjay_iid_t security_iid = ANJAY_ID_INVALID;
    if (anjay_security_object_add_instance(anjay, &security, &security_iid)) {
        ESP_LOGE(TAG, "Failed to add Security instance from stored credentials");
        return -1;
    }
    
    // Add Server object instance for LwM2M Server
    // Using parameters from ThingsBoard config:
    // - SSID: 123 (shortServerId from your JSON)
    // - Lifetime: 300s
    // - Binding: U (UDP)
    const anjay_server_instance_t server = {
        .ssid = g_provision_state.server_ssid,  // 123
        .lifetime = 300,
        .default_min_period = 1,   // From your JSON: defaultMinPeriod
        .default_max_period = -1,  // Let server set via Write-Attributes
        .disable_timeout = -1,
        .binding = "U"
    };
    
    anjay_iid_t server_iid = ANJAY_ID_INVALID;
    if (anjay_server_object_add_instance(anjay, &server, &server_iid)) {
        ESP_LOGE(TAG, "Failed to add Server instance from stored credentials");
        return -1;
    }
    
    ESP_LOGI(TAG, "Provisioned credentials loaded successfully (SSID=%d, URI=%s)",
             g_provision_state.server_ssid, g_provision_state.server_uri);
    return 0;
}

int thingsboard_clear_credentials(void) {
    ESP_LOGI(TAG, "Clearing provisioned credentials (factory reset)");
    
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for erasing");
        return -1;
    }
    
    nvs_erase_all(nvs);
    err = nvs_commit(nvs);
    nvs_close(nvs);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase NVS");
        return -1;
    }
    
    memset(&g_provision_state, 0, sizeof(g_provision_state));
    ESP_LOGI(TAG, "Credentials cleared successfully");
    return 0;
}
