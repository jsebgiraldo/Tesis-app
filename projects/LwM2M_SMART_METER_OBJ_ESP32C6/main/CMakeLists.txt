idf_component_register(
    SRCS
        "wifi_provisioning_new.c"
        "main.c"
        "wifi_provisioning.c"
        "lwm2m_client.c"
        "qrcode.c"
        "device_object.c"
        "location_object.c"
        "firmware_update.c"
        "smart_meter_object.c"
        "power_model.c"
        "energy_accumulator.c"
        "connectivity_object.c"
        "bac19_object.c"
    INCLUDE_DIRS "." "${IDF_PATH}/components/app_update/include"
    REQUIRES freertos esp_netif esp_wifi nvs_flash lwip anjay-esp-idf wifi_provisioning protocomm bt driver app_update esp_partition esp_https_ota esp_app_format esp_http_client json
    PRIV_REQUIRES app_update
)

# Ensure app_update headers are visible when building this component
target_include_directories(${COMPONENT_LIB} PRIVATE "${IDF_PATH}/components/app_update/include")
