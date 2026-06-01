idf_component_register(
    SRCS "gps.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_event 
    PRIV_REQUIRES esp_tinygpsplusplus esp_driver_uart
)
