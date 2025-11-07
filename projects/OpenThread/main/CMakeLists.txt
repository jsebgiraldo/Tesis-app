idf_component_register(
    SRCS "USAGE_EXAMPLES.c" "ot_custom_commands.c" "ot_auto_discovery.c" "ot_cli_main.c" "ot_auto_connect.c"
    INCLUDE_DIRS "."
    REQUIRES openthread esp_netif esp_event nvs_flash console
)
