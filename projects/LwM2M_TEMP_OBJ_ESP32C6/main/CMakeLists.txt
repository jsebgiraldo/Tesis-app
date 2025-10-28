idf_component_register(
    # WiFi provisioning files commented out - using Thread only
    # Remove "wifi_provisioning_new.c" "wifi_provisioning.c" to exclude from build
    SRCS "thread_joiner.c" "led_status.c" "main.c" "lwm2m_client.c" "qrcode.c" "device_object.c" "temp_object.c" "humidity_object.c" "onoff_object.c" "connectivity_object.c" "firmware_update.c" "location_object.c" "bac19_object.c"
    INCLUDE_DIRS "." "${IDF_PATH}/components/app_update/include"
    # Keep WiFi/BLE components in REQUIRES to satisfy build system dependencies
    REQUIRES freertos esp_netif esp_wifi nvs_flash lwip anjay-esp-idf wifi_provisioning protocomm bt led_strip driver app_update esp_partition esp_https_ota esp_app_format esp_http_client json
    PRIV_REQUIRES app_update openthread
)

# Ensure app_update headers are visible when building this component
target_include_directories(${COMPONENT_LIB} PRIVATE "${IDF_PATH}/components/app_update/include")


