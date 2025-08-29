#pragma once
#include "esp_event.h"
#include "esp_netif.h"
#include <stdbool.h>

typedef enum {
    NET_IF_WIFI_STA = 0,
    NET_IF_ETH,
    NET_IF_THREAD, // placeholder (requiere SoC compatible)
} net_if_t;

void net_common_init(void);
void net_common_create_interfaces(bool enable_wifi, bool enable_eth, bool enable_thread);
bool net_common_wait_ip(net_if_t ifx, int timeout_ms);

esp_netif_t* net_common_get_netif(net_if_t ifx);
