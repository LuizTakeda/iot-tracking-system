#include "esp_event.h"

// Declara o event base
ESP_EVENT_DECLARE_BASE(MY_EVENT_BASE);

// Define os IDs dos eventos como enumeração
typedef enum {
    MY_EVENT_ID_1,
    MY_EVENT_ID_2,
    MY_EVENT_ID_3,
} my_event_id_t;    