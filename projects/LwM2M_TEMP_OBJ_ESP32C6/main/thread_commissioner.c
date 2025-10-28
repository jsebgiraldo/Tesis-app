// OpenThread Commissioner Implementation
#include "thread_commissioner.h"
#include "sdkconfig.h"

#if CONFIG_OPENTHREAD_ENABLED

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "openthread/commissioner.h"
#include "openthread/instance.h"
#include "openthread/thread.h"
#include "openthread/dataset.h"

static const char *TAG = "thread_comm";
static thread_commissioner_event_cb_t g_event_callback = NULL;

// Commissioner state change callback
static void commissioner_state_callback(otCommissionerState aState, void *aContext)
{
    const char *state_str = "unknown";
    
    switch (aState) {
        case OT_COMMISSIONER_STATE_DISABLED:
            state_str = "disabled";
            break;
        case OT_COMMISSIONER_STATE_PETITION:
            state_str = "petition";
            break;
        case OT_COMMISSIONER_STATE_ACTIVE:
            state_str = "active";
            ESP_LOGI(TAG, "✓ Commissioner is now ACTIVE");
            break;
    }
    
    ESP_LOGI(TAG, "Commissioner state changed: %s", state_str);
    
    if (g_event_callback) {
        g_event_callback("state_changed", state_str);
    }
}

// Joiner event callback
static void joiner_callback(otCommissionerJoinerEvent aEvent,
                           const otJoinerInfo *aJoinerInfo,
                           const otExtAddress *aJoinerId,
                           void *aContext)
{
    char eui64_str[24];
    if (aJoinerId) {
        snprintf(eui64_str, sizeof(eui64_str), 
                "%02x%02x%02x%02x%02x%02x%02x%02x",
                aJoinerId->m8[0], aJoinerId->m8[1], aJoinerId->m8[2], aJoinerId->m8[3],
                aJoinerId->m8[4], aJoinerId->m8[5], aJoinerId->m8[6], aJoinerId->m8[7]);
    } else {
        snprintf(eui64_str, sizeof(eui64_str), "any");
    }
    
    const char *event_str = "unknown";
    switch (aEvent) {
        case OT_COMMISSIONER_JOINER_START:
            event_str = "joiner_start";
            ESP_LOGI(TAG, "📱 Joiner START: %s", eui64_str);
            break;
        case OT_COMMISSIONER_JOINER_CONNECTED:
            event_str = "joiner_connected";
            ESP_LOGI(TAG, "✓ Joiner CONNECTED: %s", eui64_str);
            break;
        case OT_COMMISSIONER_JOINER_FINALIZE:
            event_str = "joiner_finalize";
            ESP_LOGI(TAG, "✓ Joiner FINALIZE: %s", eui64_str);
            break;
        case OT_COMMISSIONER_JOINER_END:
            event_str = "joiner_end";
            ESP_LOGI(TAG, "✓ Joiner END (success): %s", eui64_str);
            break;
        case OT_COMMISSIONER_JOINER_REMOVED:
            event_str = "joiner_removed";
            ESP_LOGI(TAG, "❌ Joiner REMOVED: %s", eui64_str);
            break;
    }
    
    if (g_event_callback) {
        g_event_callback(event_str, eui64_str);
    }
}

int thread_commissioner_init(void)
{
    ESP_LOGI(TAG, "Initializing Thread Commissioner");
    
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        ESP_LOGE(TAG, "Failed to get OpenThread instance");
        return -1;
    }
    
    // Set commissioner callbacks
    esp_openthread_lock_acquire(portMAX_DELAY);
    
    otError error = otCommissionerSetStateCallback(instance, 
                                                   commissioner_state_callback, 
                                                   NULL);
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to set commissioner state callback: %d", error);
        esp_openthread_lock_release();
        return -1;
    }
    
    error = otCommissionerSetJoinerCallback(instance, joiner_callback, NULL);
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to set joiner callback: %d", error);
        esp_openthread_lock_release();
        return -1;
    }
    
    esp_openthread_lock_release();
    
    ESP_LOGI(TAG, "Thread Commissioner initialized successfully");
    return 0;
}

int thread_commissioner_start(void)
{
    ESP_LOGI(TAG, "Starting Commissioner...");
    
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        ESP_LOGE(TAG, "OpenThread instance not available");
        return -1;
    }
    
    esp_openthread_lock_acquire(portMAX_DELAY);
    
    otError error = otCommissionerStart(instance, 
                                       NULL,  // State callback (already set)
                                       NULL,  // Joiner callback (already set)
                                       NULL); // Context
    
    esp_openthread_lock_release();
    
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to start commissioner: %d", error);
        return -1;
    }
    
    ESP_LOGI(TAG, "Commissioner start request sent");
    return 0;
}

int thread_commissioner_stop(void)
{
    ESP_LOGI(TAG, "Stopping Commissioner...");
    
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        return -1;
    }
    
    esp_openthread_lock_acquire(portMAX_DELAY);
    otError error = otCommissionerStop(instance);
    esp_openthread_lock_release();
    
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to stop commissioner: %d", error);
        return -1;
    }
    
    ESP_LOGI(TAG, "Commissioner stopped");
    return 0;
}

int thread_commissioner_add_joiner(const uint8_t *eui64, const char *pskd, uint32_t timeout)
{
    if (!pskd) {
        ESP_LOGE(TAG, "PSKd is required");
        return -1;
    }
    
    // Validate PSKd (6-32 uppercase alphanumeric chars)
    size_t pskd_len = strlen(pskd);
    if (pskd_len < 6 || pskd_len > 32) {
        ESP_LOGE(TAG, "PSKd must be 6-32 characters");
        return -1;
    }
    
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        ESP_LOGE(TAG, "OpenThread instance not available");
        return -1;
    }
    
    esp_openthread_lock_acquire(portMAX_DELAY);
    
    otExtAddress *addr = NULL;
    otExtAddress joiner_eui64;
    
    if (eui64) {
        memcpy(joiner_eui64.m8, eui64, 8);
        addr = &joiner_eui64;
        
        ESP_LOGI(TAG, "Adding joiner: %02x%02x%02x%02x%02x%02x%02x%02x PSKd=%s timeout=%lus",
                 eui64[0], eui64[1], eui64[2], eui64[3],
                 eui64[4], eui64[5], eui64[6], eui64[7],
                 pskd, (unsigned long)timeout);
    } else {
        ESP_LOGI(TAG, "Adding ANY joiner with PSKd=%s timeout=%lus", 
                 pskd, (unsigned long)timeout);
    }
    
    otError error = otCommissionerAddJoiner(instance, addr, pskd, timeout);
    
    esp_openthread_lock_release();
    
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to add joiner: %d", error);
        return -1;
    }
    
    ESP_LOGI(TAG, "✓ Joiner added successfully");
    return 0;
}

int thread_commissioner_remove_joiner(const uint8_t *eui64)
{
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        return -1;
    }
    
    esp_openthread_lock_acquire(portMAX_DELAY);
    
    otExtAddress *addr = NULL;
    otExtAddress joiner_eui64;
    
    if (eui64) {
        memcpy(joiner_eui64.m8, eui64, 8);
        addr = &joiner_eui64;
    }
    
    otError error = otCommissionerRemoveJoiner(instance, addr);
    
    esp_openthread_lock_release();
    
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to remove joiner: %d", error);
        return -1;
    }
    
    ESP_LOGI(TAG, "Joiner removed");
    return 0;
}

bool thread_commissioner_is_active(void)
{
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        return false;
    }
    
    esp_openthread_lock_acquire(portMAX_DELAY);
    otCommissionerState state = otCommissionerGetState(instance);
    esp_openthread_lock_release();
    
    return (state == OT_COMMISSIONER_STATE_ACTIVE);
}

int thread_commissioner_get_credentials(char *network_name, char *network_key,
                                       uint16_t *panid, uint8_t *channel)
{
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        return -1;
    }
    
    esp_openthread_lock_acquire(portMAX_DELAY);
    
    // Get active dataset
    otOperationalDataset dataset;
    otError error = otDatasetGetActive(instance, &dataset);
    
    if (error != OT_ERROR_NONE) {
        esp_openthread_lock_release();
        ESP_LOGE(TAG, "Failed to get active dataset: %d", error);
        return -1;
    }
    
    // Extract network name
    if (network_name && dataset.mComponents.mIsNetworkNamePresent) {
        strncpy(network_name, dataset.mNetworkName.m8, 
                OT_NETWORK_NAME_MAX_SIZE);
        network_name[OT_NETWORK_NAME_MAX_SIZE] = '\0';
    }
    
    // Extract network key (master key)
    if (network_key && dataset.mComponents.mIsNetworkKeyPresent) {
        for (int i = 0; i < OT_NETWORK_KEY_SIZE; i++) {
            sprintf(&network_key[i * 2], "%02x", dataset.mNetworkKey.m8[i]);
        }
        network_key[OT_NETWORK_KEY_SIZE * 2] = '\0';
    }
    
    // Extract PAN ID
    if (panid && dataset.mComponents.mIsPanIdPresent) {
        *panid = dataset.mPanId;
    }
    
    // Extract channel
    if (channel && dataset.mComponents.mIsChannelPresent) {
        *channel = dataset.mChannel;
    }
    
    esp_openthread_lock_release();
    
    return 0;
}

void thread_commissioner_register_callback(thread_commissioner_event_cb_t callback)
{
    g_event_callback = callback;
}

#endif // CONFIG_OPENTHREAD_ENABLED
