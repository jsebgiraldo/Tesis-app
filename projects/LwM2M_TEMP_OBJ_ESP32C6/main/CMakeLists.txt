idf_component_register(
    SRCS "led_status.c" "main.c" "temp_object.c" "wifi_provisioning.c" "lwm2m_client.c" "qrcode.c"
    INCLUDE_DIRS "."
    REQUIRES freertos esp_netif esp_wifi nvs_flash lwip anjay-esp-idf wifi_provisioning protocomm bt led_strip driver
)
