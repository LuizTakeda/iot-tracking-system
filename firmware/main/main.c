#include <stdio.h>
#include "command_interface_events.h"
#include "command_interface.h"
#include "system_api.h"
#include "gps.h"
#include "wifi.h"

#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "gps_events.h"

void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  if (event_base != GPS_EVENTS)
  {
    return;
  }

  switch (event_id)
  {
  case GPS_EVENT_DATA_READY:
  {
    gps_data_ready_payload_t* payload = (gps_data_ready_payload_t*)event_data;

    ESP_LOGI("TEST",
         "Lat: %.6f | Lon: %.6f | Alt: %.2f m | Speed: %.2f km/h | "
         "Course: %.2f deg | Sats: %lu | HDOP: %lu | Timestamp: %lld",
         payload->latitude,
         payload->longitude,
         payload->altitude,
         payload->speed_kmh,
         payload->course_deg,
         (unsigned long)payload->satellites,
         (unsigned long)payload->hdop,
         (long long)payload->timestamp);
  }
  break;

  default:
    break;
  }
};

void app_main(void)
{
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_ERROR_CHECK(wifi_initialization());
  ESP_ERROR_CHECK(system_api_initialization());
  ESP_ERROR_CHECK(command_interface_initialization());
  ESP_ERROR_CHECK(gps_initialization());

  ESP_ERROR_CHECK(esp_event_handler_instance_register(GPS_EVENTS, GPS_EVENT_DATA_READY, &event_handler, NULL, NULL));
}
