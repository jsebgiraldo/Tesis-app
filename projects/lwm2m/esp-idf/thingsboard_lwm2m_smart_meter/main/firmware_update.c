/*
 * Firmware Update support for Anjay on ESP-IDF
 */

#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

#include <avsystem/commons/avs_log.h>

#include <anjay/anjay.h>
#include <anjay/fw_update.h>

#include <esp_err.h>
#include <esp_partition.h>
#include <esp_system.h>

// Minimal forward declarations for OTA APIs to decouple from esp_ota_ops.h
// NOTE: Linkage is provided by the app_update component.
typedef int esp_ota_handle_t;
typedef enum {
    ESP_OTA_IMG_UNDEFINED = 0,
    ESP_OTA_IMG_NEW = 1,
    ESP_OTA_IMG_PENDING_VERIFY = 2,
    ESP_OTA_IMG_VALID = 3,
    ESP_OTA_IMG_INVALID = 4
} esp_ota_img_states_t;
// esp_ota_ops.h defines OTA_SIZE_UNKNOWN as 0; define locally if missing
#ifndef OTA_SIZE_UNKNOWN
#define OTA_SIZE_UNKNOWN 0
#endif
esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t image_size, esp_ota_handle_t *out_handle);
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size);
esp_err_t esp_ota_end(esp_ota_handle_t handle);
esp_err_t esp_ota_abort(esp_ota_handle_t handle);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition);
const esp_partition_t *esp_ota_get_running_partition(void);
esp_err_t esp_ota_get_state_partition(const esp_partition_t *partition, esp_ota_img_states_t *out_state);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *start_from);

#include "firmware_update.h"
#include "sdkconfig.h"

static struct {
    anjay_t *anjay;
    esp_ota_handle_t update_handle;
    const esp_partition_t *update_partition;
    atomic_bool update_requested;
} fw_state;

static int fw_stream_open(void *user_ptr,
                          const char *package_uri,
                          const struct anjay_etag *package_etag) {
    (void) user_ptr;
    (void) package_uri;
    (void) package_etag;

    assert(!fw_state.update_partition);

    fw_state.update_partition = esp_ota_get_next_update_partition(NULL);
    if (!fw_state.update_partition) {
        avs_log(fw_update, ERROR, "Cannot obtain update partition");
        return -1;
    }

    if (esp_ota_begin(fw_state.update_partition, OTA_SIZE_UNKNOWN,
                      &fw_state.update_handle)) {
        avs_log(fw_update, ERROR, "OTA begin failed");
        fw_state.update_partition = NULL;
        return -1;
    }
    return 0;
}

static int fw_stream_write(void *user_ptr, const void *data, size_t length) {
    (void) user_ptr;

    assert(fw_state.update_partition);

    int result = esp_ota_write(fw_state.update_handle, data, length);
    if (result) {
        avs_log(fw_update, ERROR, "OTA write failed");
#ifdef ESP_ERR_OTA_VALIDATE_FAILED
        return result == ESP_ERR_OTA_VALIDATE_FAILED
                       ? ANJAY_FW_UPDATE_ERR_UNSUPPORTED_PACKAGE_TYPE
                       : -1;
#else
        return -1;
#endif
    }
    return 0;
}

static int fw_stream_finish(void *user_ptr) {
    (void) user_ptr;

    assert(fw_state.update_partition);

    int result = esp_ota_end(fw_state.update_handle);
    if (result) {
        avs_log(fw_update, ERROR, "OTA end failed");
        fw_state.update_partition = NULL;
#ifdef ESP_ERR_OTA_VALIDATE_FAILED
        return result == ESP_ERR_OTA_VALIDATE_FAILED
                       ? ANJAY_FW_UPDATE_ERR_INTEGRITY_FAILURE
                       : -1;
#else
        return -1;
#endif
    }
    return 0;
}

static void fw_reset(void *user_ptr) {
    (void) user_ptr;

    if (fw_state.update_partition) {
        esp_ota_abort(fw_state.update_handle);
        fw_state.update_partition = NULL;
    }
}

static int fw_perform_upgrade(void *user_ptr) {
    (void) user_ptr;

    int result = esp_ota_set_boot_partition(fw_state.update_partition);
    if (result) {
        fw_state.update_partition = NULL;
#ifdef ESP_ERR_OTA_VALIDATE_FAILED
        return result == ESP_ERR_OTA_VALIDATE_FAILED
                       ? ANJAY_FW_UPDATE_ERR_INTEGRITY_FAILURE
                       : -1;
#else
        return -1;
#endif
    }

    // We use Anjay's event loop and request a reboot after loop interruption
    if (anjay_event_loop_interrupt(fw_state.anjay)) {
        return -1;
    }

    atomic_store(&fw_state.update_requested, true);
    return 0;
}

static const anjay_fw_update_handlers_t HANDLERS = {
    .stream_open = fw_stream_open,
    .stream_write = fw_stream_write,
    .stream_finish = fw_stream_finish,
    .reset = fw_reset,
    .perform_upgrade = fw_perform_upgrade,
};

int fw_update_install(anjay_t *anjay) {
    anjay_fw_update_initial_state_t state = { 0 };
    const esp_partition_t *partition = esp_ota_get_running_partition();
    esp_ota_img_states_t partition_state;
    esp_ota_get_state_partition(partition, &partition_state);

    if (partition_state == ESP_OTA_IMG_UNDEFINED
            || partition_state == ESP_OTA_IMG_PENDING_VERIFY) {
        avs_log(fw_update, INFO, "First boot from partition with new firmware");
        esp_ota_mark_app_valid_cancel_rollback();
        state.result = ANJAY_FW_UPDATE_INITIAL_SUCCESS;
    }

    // make sure this module is installed for single Anjay instance only
    assert(!fw_state.anjay);
    fw_state.anjay = anjay;

    return anjay_fw_update_install(anjay, &HANDLERS, NULL, &state);
}

bool fw_update_requested(void) {
    return atomic_load(&fw_state.update_requested);
}

void fw_update_reboot(void) {
    avs_log(fw_update, INFO, "Rebooting to perform a firmware upgrade...");
    esp_restart();
}
