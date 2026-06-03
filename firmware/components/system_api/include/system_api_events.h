#pragma once

#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(SYSTEM_API);

//**************************************************
// Events
//**************************************************

enum
{
    SYSTEM_API_EVENT_CONNECTED, 
    SYSTEM_API_EVENT_DISCONNECTED, 
};

//**************************************************
// Payloads
//**************************************************
