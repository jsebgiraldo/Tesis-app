#include "thread_prov.h"
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <esp_log.h>
#include <esp_err.h>
#include <nvs_flash.h>
#if CONFIG_OPENTHREAD_ENABLED
#include <esp_openthread.h>
#include <openthread/instance.h>
#include <openthread/joiner.h>
#include <openthread/thread.h>
#include <openthread/ip6.h>
#endif
#include "sdkconfig.h"

#if CONFIG_OPENTHREAD_ENABLED
__attribute__((unused)) static const char *TAG = "thread_prov";
__attribute__((unused)) static bool s_join_started = false;
__attribute__((unused)) static bool s_attached_logged = false;
#endif // CONFIG_OPENTHREAD_ENABLED

#if CONFIG_OPENTHREAD_ENABLED

static void joiner_cb(otError error, void *context) {
    (void) context;
    ESP_LOGI(TAG, "Joiner result: %d", error);
    if (error == OT_ERROR_NONE) {
        otInstance *ins = esp_openthread_get_instance();
        otError err = otThreadSetEnabled(ins, true);
        if (err != OT_ERROR_NONE) {
            ESP_LOGE(TAG, "Failed to enable Thread after join: %d", err);
        } else {
            ESP_LOGI(TAG, "Thread enabled after successful commissioning");
        }
    } else {
        ESP_LOGE(TAG, "Joiner failed (error=%d). Check PSKd / Border Router logs.", error);
    }
}

static bool start_joiner(void) {
#if CONFIG_LWM2M_THREAD_JOINER
    const char *pskd = CONFIG_LWM2M_THREAD_JOINER_PSKD;
    if (!pskd || !*pskd) {
        ESP_LOGE(TAG, "Empty PSKd; set CONFIG_LWM2M_THREAD_JOINER_PSKD");
        return false;
    }
    otInstance *ins = esp_openthread_get_instance();
    otError err = otJoinerStart(ins, pskd, NULL, NULL, NULL, joiner_cb, NULL);
    if (err != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "otJoinerStart failed: %d", err);
        return false;
    }
    ESP_LOGI(TAG, "Joiner started with PSKd '%s'", pskd);
    return true;
#else
    ESP_LOGW(TAG, "Joiner not enabled (CONFIG_LWM2M_THREAD_JOINER disabled)");
    return false;
#endif
}

bool thread_prov_start(void) {
#if !CONFIG_LWM2M_NETWORK_USE_THREAD
    return false;
#else
    if (s_join_started) {
        return true;
    }
    esp_openthread_platform_config_t cfg = ESP_OPENTHREAD_DEFAULT_PLATFORM_CONFIG();
    ESP_ERROR_CHECK(esp_openthread_init(&cfg));
    otInstance *ins = esp_openthread_get_instance();
    if (!ins) {
        ESP_LOGE(TAG, "No OpenThread instance after init");
        return false;
    }
    // If already commissioned (dataset active and enabled), skip joiner
    if (otThreadGetDeviceRole(ins) != OT_DEVICE_ROLE_DISABLED) {
        ESP_LOGI(TAG, "Thread already active (role=%d)", otThreadGetDeviceRole(ins));
        s_join_started = true;
        return true;
    }
    bool started = start_joiner();
    s_join_started = started;
    return started;
#endif
}

bool thread_prov_is_attached(void) {
#if !CONFIG_LWM2M_NETWORK_USE_THREAD
    return false;
#else
    otInstance *ins = esp_openthread_get_instance();
    if (!ins) return false;
    otDeviceRole role = otThreadGetDeviceRole(ins);
    bool attached = (role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER || role == OT_DEVICE_ROLE_LEADER);
    if (attached && !s_attached_logged) {
        ESP_LOGI(TAG, "Thread attached (role=%d)", role);
        s_attached_logged = true;
    }
    return attached;
#endif
}

bool thread_prov_get_ip(char *buf, size_t buf_len) {
#if !CONFIG_LWM2M_NETWORK_USE_THREAD
    return false;
#else
    if (!buf || buf_len < 4) return false;
    otInstance *ins = esp_openthread_get_instance();
    if (!ins) return false;
    const otNetifAddress *addr = otIp6GetUnicastAddresses(ins);
    while (addr) {
        if (!addr->mAddress.mFields.m8[0]) { // skip unspecified
            addr = addr->mNext;
            continue;
        }
        // Prefer mesh-local (starts with fd or fe80 for link local - choose mesh-local unique local fd..)
        uint8_t first = addr->mAddress.mFields.m8[0];
        if (first == 0xfd || first == 0x20 || first == 0x30) {
            char tmp[OT_IP6_ADDRESS_STRING_SIZE];
            otIp6AddressToString(&addr->mAddress, tmp, sizeof(tmp));
            strlcpy(buf, tmp, buf_len);
            return true;
        }
        addr = addr->mNext;
    }
    return false;
#endif
}

#else // !CONFIG_OPENTHREAD_ENABLED
bool thread_prov_start(void){return false;} 
bool thread_prov_is_attached(void){return false;} 
bool thread_prov_get_ip(char *b,size_t l){(void)b;(void)l;return false;}
#endif
