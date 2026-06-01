idf_component_register(
    SRCS "command_interface.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event
    PRIV_REQUIRES esp_wifi esp_driver_uart
)
