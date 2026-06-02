#pragma once

#include "esp_event.h"
#include <time.h>

ESP_EVENT_DECLARE_BASE(GPS_EVENT);

//**************************************************
// Events
//**************************************************

enum
{
    GPS_EVENT_DATA_READY, 
};

//**************************************************
// Payloads
//**************************************************

typedef struct {    
    double latitude;
    double longitude;
    double altitude;
    double speed_kmh;          
    double course_deg;         
    uint32_t satellites;
    int32_t hdop;        
    time_t timestamp;     
} gps_data_ready_payload_t;    