# Component CMake for common

set(SRCS "wifi.c" "net_common.c")
set(REQUIRES esp_wifi nvs_flash esp_event esp_netif)

if(CONFIG_MQTT_USE_ETH AND CONFIG_ETH_ENABLED)
    list(APPEND SRCS "ethernet.c")
    list(APPEND REQUIRES esp_eth)
endif()

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "include"
                       REQUIRES ${REQUIRES})
