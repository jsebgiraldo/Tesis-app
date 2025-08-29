#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t eth_init_default(void); // RMII default pins by board
void eth_start(void);
void eth_stop(void);
bool eth_is_connected(void);
