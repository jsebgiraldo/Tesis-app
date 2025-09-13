#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int qrcode_ecc_level;
    int qrcode_version;
} esp_qrcode_config_t;

#define ESP_QRCODE_CONFIG_DEFAULT() { \
    .qrcode_ecc_level = 0, \
    .qrcode_version = 10 \
}

void esp_qrcode_generate(esp_qrcode_config_t *cfg, const char *text);

#ifdef __cplusplus
}
#endif
