# Unit tests for OpenThread + LwM2M integration

idf_component_register(
    SRCS 
        "test_thread_config.c"
        "test_thread_network.c"
    INCLUDE_DIRS 
        "."
        "../main"
    REQUIRES 
        unity
        openthread
        esp_openthread
        main
)
