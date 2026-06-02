#include "gps.h"
#include "gps_events.h"
#include "TinyGPSPlus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include <time.h>

//**************************************************
// Defines
//**************************************************

#define UART_NUM UART_NUM_2
#define TX_PIN GPIO_NUM_17
#define RX_PIN GPIO_NUM_16
#define BAUD_RATE 9600
#define UART_BUF_SIZE (1024 * 2)
#define UART_QUEUE_SIZE 20

ESP_EVENT_DEFINE_BASE(GPS_EVENTS);

//**************************************************
// Globals
//**************************************************

static const char TAG[] = "gps";
static TinyGPSPlus gps;
static QueueHandle_t uart_queue = NULL;

//**************************************************
// Function Prototypes
//**************************************************

static void gps_task(void *pvParameters);

//**************************************************
// Public Functions
//**************************************************

esp_err_t gps_initialization()
{
  uart_config_t uart_config = {
      .baud_rate = BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .rx_flow_ctrl_thresh = 0,
      .source_clk = UART_SCLK_DEFAULT,
      .flags = {
          .allow_pd = 0,
          .backup_before_sleep = 0,
      },
  };

  esp_err_t ret = uart_driver_install(UART_NUM, UART_BUF_SIZE, 0, UART_QUEUE_SIZE, &uart_queue, 0);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to install UART driver");
    return ret;
  }

  ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));

  ESP_ERROR_CHECK(uart_set_pin(UART_NUM, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

  BaseType_t task_ret = xTaskCreate(gps_task, "gps_task", 4096, NULL, 6, NULL);
  if (task_ret != pdPASS)
  {
    ESP_LOGE(TAG, "Failed to create GPS task");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "GPS Driver initialized successfully");
  return ESP_OK;
}

//**************************************************
// Private Functions
//**************************************************

static void gps_task(void *pvParameters)
{
  uart_event_t event;
  uint8_t buf[128];

  while (1)
  {
    if (xQueueReceive(uart_queue, (void *)&event, portMAX_DELAY))
    {
      if (event.type == UART_DATA && event.size > 0)
      {
        size_t to_read = (event.size > sizeof(buf)) ? sizeof(buf) : event.size;
        int size = uart_read_bytes(UART_NUM, buf, to_read, portMAX_DELAY);

        if (size <= 0)
        {
          continue;
        }

        bool valid_sentence = false;

        for (int i = 0; i < size; i++)
        {
          valid_sentence |= gps.encode(buf[i]);
        }

        if (valid_sentence && gps.location.isValid())
        {
          gps_data_ready_payload_t payload = {
              .latitude = gps.location.lat(),
              .longitude = gps.location.lng(),
              .altitude = gps.altitude.meters(), 
              .speed_kmh = gps.speed.isValid() ? gps.speed.kmph() : 0.0,
              .course_deg = gps.course.isValid() ? gps.course.deg() : 0.0,
              .satellites = gps.satellites.value(),
              .hdop = gps.hdop.value(), 
              .timestamp = 0,
          };

          if (payload.speed_kmh < 3.0)
          {
            payload.speed_kmh = 0.0;
            payload.course_deg = -1.0;
          }

          if (gps.date.isValid() && gps.time.isValid())
          {
            struct tm time_info;
            time_info.tm_year = gps.date.year() - 1900;
            time_info.tm_mon = gps.date.month() - 1;
            time_info.tm_mday = gps.date.day();
            time_info.tm_hour = gps.time.hour();
            time_info.tm_min = gps.time.minute();
            time_info.tm_sec = gps.time.second();
            time_info.tm_isdst = 0; 
            payload.timestamp = mktime(&time_info);
          }

          esp_err_t err = esp_event_post(
              GPS_EVENTS,
              GPS_EVENT_DATA_READY,
              &payload,
              sizeof(payload),
              portMAX_DELAY);

          if (err != ESP_OK)
          {
            ESP_LOGE(TAG, "Failed to post event: %s",
                     esp_err_to_name(err));
          }
        }
        else if (valid_sentence && !gps.location.isValid())
        {
          ESP_LOGD(TAG, "NMEA parsed, waiting for fix. Satellites tracked: %ld", gps.satellites.value());
        }
      }
    }
  }
  vTaskDelete(NULL);
}