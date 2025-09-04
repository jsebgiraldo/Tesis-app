idf_component_register(
    SRCS "main.c" "temp_object.c" "wifi.c" "lwm2m_client.c"
    INCLUDE_DIRS "."
    REQUIRES freertos esp_netif esp_wifi nvs_flash lwip anjay-esp-idf
)
