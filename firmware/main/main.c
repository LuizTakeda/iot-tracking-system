#include <stdio.h>
#include "command_interface.h"
#include "gps.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "esp_err.h"

//**************************************************
// Defines
//**************************************************

//**************************************************
// Globals
//**************************************************

//**************************************************
// Public Functions
//**************************************************

void app_main(void) {
    ESP_ERROR_CHECK(command_interface_initialization());
}

//**************************************************
// Private Functions
//**************************************************