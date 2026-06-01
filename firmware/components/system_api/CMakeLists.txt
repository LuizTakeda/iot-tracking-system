idf_component_register(
    SRCS "system_api.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event 
    PRIV_REQUIRES esp_wifi nvs_flash mqtt mbedtls
)
