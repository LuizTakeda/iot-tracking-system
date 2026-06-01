#pragma once

#include "esp_err.h"

//**************************************************
// Public Functions
//**************************************************

/**
 * @brief Initializes the command interface module.
 *
 * This function sets up the default event loop, configures UART 0, 
 * installs the UART driver, and spawns the FreeRTOS task responsible 
 * for reading and parsing terminal commands.
 *
 * @return 
 * - ESP_OK on success.
 * - ESP_FAIL if the FreeRTOS task creation fails.
 * - Error code returned by esp_event_loop_create_default() or UART configuration APIs.
 */
esp_err_t command_interface_initialization(void);