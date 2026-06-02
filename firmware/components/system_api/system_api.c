#include <stdio.h>
#include "system_api_events.h"
#include "system_api.h"
#include "esp_crt_bundle.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif_sntp.h"
#include <time.h>

//**************************************************
// Defines
//**************************************************

#define MQTT_BROKKER_URL CONFIG_SYSTEM_API_MQTT_BROKER_URL
#define MQTT_USERNAME CONFIG_SYSTEM_API_MQTT_USERNAME
#define MQTT_PASSWORD CONFIG_SYSTEM_API_MQTT_PASSWORD

//**************************************************
// Globals
//**************************************************

static const char TAG[] = "system_api";

static esp_mqtt_client_handle_t s_client = NULL;

static bool s_is_connected = false;

//**************************************************
// Function Prototypes
//**************************************************

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void time_sync_cb(struct timeval *tv);
static void sntp_sync_task(void *pvParameters);

//**************************************************
// Public Functions
//**************************************************

esp_err_t system_api_initialization()
{
  const esp_mqtt_client_config_t mqtt_cfg = {
      .broker = {
          .address.uri = MQTT_BROKKER_URL,
          .verification.crt_bundle_attach = esp_crt_bundle_attach,
      },
      .credentials = {
          .username = MQTT_USERNAME,
          .authentication.password = MQTT_PASSWORD,
      },
      .session = {
          .keepalive = 60,
          .last_will = {
              .topic = "/tracking_device/tracking-one/status",
              .msg = "{\"online\": false}",
              .retain = true,
              .qos = 1,
          },
      }};

  s_client = esp_mqtt_client_init(&mqtt_cfg);
  if (s_client == NULL)
  {
    ESP_LOGE(TAG, "Failed to initialize MQTT client");
    return ESP_FAIL;
  }

  esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  sntp_config.start = false;
  sntp_config.sync_cb = time_sync_cb;
  esp_netif_sntp_init(&sntp_config);
  setenv("TZ", "WET0WEST,M3.5.0/1,M10.5.0", 1);
  tzset();

  ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                      IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL, NULL));

  return ESP_OK;
}

//**************************************************
// Private Functions
//**************************************************

static void time_sync_cb(struct timeval *tv)
{
  ESP_LOGI(TAG, "Tempo sincronizado!");

  time_t now;
  struct tm timeinfo;
  char buffer[64];

  time(&now); // pega o tempo APÓS sincronização
  localtime_r(&now, &timeinfo);
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);

  ESP_LOGI(TAG, "Data/Hora: %s", buffer);
}

static void sntp_sync_task(void *pvParameters)
{
  int retry = 0;
  const int retry_count = 10;

  while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(2000)) != ESP_OK && ++retry < retry_count)
  {
    ESP_LOGW(TAG, "Aguardando sincronização SNTP... (%d/%d)",
             retry, retry_count);
  }

  if (retry == retry_count)
  {
    ESP_LOGE(TAG, "Falha ao sincronizar tempo via SNTP!");
    // Trate o erro aqui
  }
  else
  {
    ESP_LOGI(TAG, "Tempo sincronizado com sucesso!");
  }

  vTaskDelete(NULL); // deleta a task ao finalizar
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  if (event_base != WIFI_EVENT)
  {
    return;
  }

  switch (event_id)
  {

  case WIFI_EVENT_STA_DISCONNECTED:
    if (s_client == NULL || !s_is_connected)
    {
      return;
    }

    esp_mqtt_client_stop(s_client);
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
    esp_netif_sntp_start();

    xTaskCreate(sntp_sync_task, "sntp_sync", 2048, NULL, 5, NULL);

    if (s_is_connected)
    {
      esp_mqtt_client_reconnect(s_client);
    }
    else
    {
      esp_mqtt_client_start(s_client);
    }
    break;

  default:
    break;
  }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
  esp_mqtt_event_handle_t event = event_data;
  esp_mqtt_client_handle_t client = event->client;

  int msg_id;

  switch ((esp_mqtt_event_id_t)event_id)
  {
  case MQTT_EVENT_CONNECTED:
    s_is_connected = true;
    ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
    msg_id = esp_mqtt_client_publish(client, "/tracking_device/tracking-one/status", "{\"online\": true}", 0, 1, true);
    ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
    break;

  case MQTT_EVENT_DISCONNECTED:
    s_is_connected = false;
    ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
    break;

  case MQTT_EVENT_SUBSCRIBED:
    ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
    break;

  case MQTT_EVENT_DATA:
    ESP_LOGI(TAG, "MQTT_EVENT_DATA");
    break;

  case MQTT_EVENT_ERROR:
    ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
    break;

  default:
    break;
  }
}
