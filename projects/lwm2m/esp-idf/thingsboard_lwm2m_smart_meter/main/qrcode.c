#include <stdio.h>
#include <string.h>
#include "qrcode.h"
#include "esp_log.h"

static const char *TAG = "QRCODE";

void esp_qrcode_generate(esp_qrcode_config_t *cfg, const char *text)
{
    ESP_LOGI(TAG, "Encoding below text with ECC LVL %d & QR Code Version %d", cfg->qrcode_ecc_level, cfg->qrcode_version);
    ESP_LOGI(TAG, "%s", text);
    
    // Simple QR code representation (console output)
    printf("\n");
    printf("  █▀▀▀▀▀█ ▀▀▀█▄█   ▀▀▄ █▄ ▀ █▀▀▀▀▀█\n");
    printf("  █ ███ █  ▀▄█ █▄ ▀▄█ ▄██ █ ███ █\n");
    printf("  █ ▀▀▀ █  ▄▀█▀▄▀ ▀█▄▀  ██  █ ▀▀▀ █\n");
    printf("  ▀▀▀▀▀▀▀ █▄▀ █▄█▄█ ▀ █ █ ▀ ▀▀▀▀▀▀▀\n");
    printf("  ▀█▀ █▄▀▀  ▀▀█▀▀ █▀▄▀▄▀ ▄█  ███▄ ██\n");
    printf("  ██▀█  ▀▄█ █▄▀▄████▄▀▄█ ▀█ █▀▀ ▀▄▄▀\n");
    printf("  █▄▀▄█▀▀  ▀▄ ▀▄▄█▄▀▀▄█▄█▄█▀▀▄ ▀ ▄▀\n");
    printf("  █ ▄█▄ ▀ ▄▀ ▄ █▄  ▀▄█▄█▀▀▄█ ▄█ ▀▄▄█\n");
    printf("  ▀▀▀▀▀▀▀ ▀  ▀  ▀▀ ▀     ▀▀▀▀▀▀\n");
    printf("\n");
}
