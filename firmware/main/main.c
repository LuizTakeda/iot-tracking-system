#include <stdio.h>
#include "command_interface_events.h"
#include "command_interface.h"
#include "system_api.h"
#include "gps.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "esp_err.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/event_groups.h"

//**************************************************
// Defines
//**************************************************

//**************************************************
// Globals
//**************************************************

static const char TAG[] = "main";

static uint8_t reconnect_attempts = 0;

//**************************************************
// Function Prototypes
//**************************************************

static void command_interface_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static esp_err_t wifi_initialization();
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static esp_err_t get_wifi_credentials(char *ssid, char *password);
static esp_err_t save_wifi_credentials(char *ssid, char *password);

//**************************************************
// Public Functions
//**************************************************

void app_main(void)
{
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  ESP_ERROR_CHECK(command_interface_initialization());
  ESP_ERROR_CHECK(wifi_initialization());
  ESP_ERROR_CHECK(system_api_initialization());

  ESP_ERROR_CHECK(
    esp_event_handler_register(
        COMMAND_INTERFACE_EVENTS,
        ESP_EVENT_ANY_ID,
        command_interface_event_handler,
        NULL));
}

//**************************************************
// Private Functions
//**************************************************

static void command_interface_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  if (event_base != COMMAND_INTERFACE_EVENTS)
  {
    return;
  }

  switch (event_id)
  {
  case CMD_EVENT_WIFI_CREDENTIALS:
  {
    if (event_data == NULL)
    {
      return;
    }

    command_interface_wifi_credentials_payload_t *payload = (command_interface_wifi_credentials_payload_t *)event_data;

    if (save_wifi_credentials(payload->ssid, payload->password) != ESP_OK)
    {
      ESP_LOGE(TAG, "Failed to save Wi-Fi credentials");
      return;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };

    strncpy((char *)wifi_config.sta.ssid, payload->ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, payload->password, sizeof(wifi_config.sta.password) - 1);

    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_connect();

    reconnect_attempts = 0;
  }
  break;

  default:
    break;
  }
}

static esp_err_t wifi_initialization()
{
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                      IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL, NULL));

  wifi_config_t wifi_config = {
      .sta = {
          .threshold.authmode = WIFI_AUTH_OPEN,
      },
  };

  esp_err_t err = get_wifi_credentials((char *)wifi_config.sta.ssid, (char *)wifi_config.sta.password);

  if (err == ESP_OK)
  {
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
  }
  else
  {
    ESP_LOGW(TAG, "No Wi-Fi credentials found");
  }

  return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  if (event_base != WIFI_EVENT)
  {
    return;
  }

  switch (event_id)
  {
  case WIFI_EVENT_STA_START:
    ESP_LOGI(TAG, "Connecting to Wi-Fi...");
    esp_wifi_connect();
    break;

  case WIFI_EVENT_STA_DISCONNECTED:
    if (reconnect_attempts >= 10)
    {
      ESP_LOGE(TAG, "Maximum reconnect attempts reached");
      return;
    }

    esp_wifi_connect();
    reconnect_attempts++;
    break;

  default:
    break;
  }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  if (event_base != IP_EVENT)
  {
    return;
  }

  switch (event_id)
  {
  case IP_EVENT_STA_GOT_IP:
  {
    reconnect_attempts = 0;
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
  }
  break;

  default:
    break;
  }
}

static esp_err_t get_wifi_credentials(char *ssid, char *password)
{
  nvs_handle_t nvs_handle;
  esp_err_t err;

  err = nvs_open("wifi", NVS_READONLY, &nvs_handle);
  if (err != ESP_OK)
    return err;

  size_t ssid_len = MAX_SSID_LEN;
  err = nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len);
  if (err != ESP_OK)
    goto cleanup;

  size_t pass_len = MAX_PASSPHRASE_LEN;
  err = nvs_get_str(nvs_handle, "password", password, &pass_len);

cleanup:
  nvs_close(nvs_handle);
  return err;
}

static esp_err_t save_wifi_credentials(char *ssid, char *password)
{
  nvs_handle_t nvs_handle;
  esp_err_t err;

  err = nvs_open("wifi", NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK)
    return err;

  err = nvs_set_str(nvs_handle, "ssid", ssid);
  if (err != ESP_OK)
    goto cleanup;

  err = nvs_set_str(nvs_handle, "password", password);
  if (err != ESP_OK)
    goto cleanup;

  err = nvs_commit(nvs_handle);

cleanup:
  nvs_close(nvs_handle);
  return err;
}