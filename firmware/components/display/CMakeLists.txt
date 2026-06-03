idf_component_register(
  SRCS "display.c"
  INCLUDE_DIRS "include"
  REQUIRES esp_event 
  PRIV_REQUIRES esp_wifi mqtt gps driver esp_driver_gpio
)
