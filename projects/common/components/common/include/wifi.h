#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t wifi_init_sta(const char* ssid, const char* pass);
void wifi_start(void);
void wifi_stop(void);
bool wifi_is_connected(void);
