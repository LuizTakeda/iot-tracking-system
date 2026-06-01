#pragma once

#include "esp_event.h"
#include "esp_wifi.h"

/**
 * @brief Declaration of the Command Interface event base.
 */
ESP_EVENT_DECLARE_BASE(COMMAND_INTERFACE_EVENTS);

/**
 * @brief Command Interface event IDs.
 */
enum
{
    CMD_EVENT_WIFI_CREDENTIALS, /**< Event triggered when valid Wi-Fi credentials are received via UART. */
};

/**
 * @brief Payload structure for the CMD_EVENT_WIFI_CREDENTIALS event.
 */
typedef struct
{
    char ssid[MAX_SSID_LEN];         /**< Buffer storing the extracted Wi-Fi SSID. */
    char password[MAX_PASSPHRASE_LEN]; /**< Buffer storing the extracted Wi-Fi password. */
} command_interface_wifi_credentials_payload_t;