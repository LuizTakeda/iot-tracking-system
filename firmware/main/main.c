#include "wifi.h"
#include "system_api.h"
#include "gps.h"
#include "command_interface.h"
#include "display.h"

#include "esp_err.h"
#include "nvs_flash.h"

void app_main(void)
{
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret); 

  ESP_ERROR_CHECK(display_initialization());
  ESP_ERROR_CHECK(wifi_initialization());
  ESP_ERROR_CHECK(system_api_initialization());
  ESP_ERROR_CHECK(gps_initialization());
  ESP_ERROR_CHECK(command_interface_initialization());
}
