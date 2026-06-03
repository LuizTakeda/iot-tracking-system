#include "display.h"
#include "ssd1306.h"

#include "gps_events.h"
#include "system_api_events.h"

#include "driver/i2c_master.h"
#include "driver/gpio.h"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <time.h>
#include <math.h>
#include <stdio.h>

#define I2C_SCL_PIN 15
#define I2C_SDA_PIN 4
#define OLED_RESET_PIN 16

#define DISPLAY_REFRESH_SECONDS 5
#define APP_TAG "DISPLAY"

static i2c_master_bus_handle_t i2c0_bus_hdl;

/* ========================================================================= */
/* Shared State                                                             */
/* ========================================================================= */

typedef struct
{
  double latitude;
  double longitude;
  double altitude;

  double speed_kmh;
  double course_deg;

  uint32_t satellites;
  int32_t hdop;

  time_t gps_timestamp;

  bool wifi_connected;
  bool mqtt_connected;

} display_state_t;

static display_state_t g_display_state = {0};
static SemaphoreHandle_t g_display_mutex = NULL;

/* ========================================================================= */
/* Forward declarations                                                    */
/* ========================================================================= */

static void i2c0_ssd1306_task(void *pvParameters);

static void event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data);

static void display_state_set_wifi(bool connected);
static void display_state_set_mqtt(bool connected);

static void display_state_update_gps(const gps_data_ready_payload_t *gps);

static display_state_t display_state_snapshot(void);

/* ========================================================================= */
/* State helpers                                                           */
/* ========================================================================= */

static void display_state_set_wifi(bool connected)
{
  if (xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(50)))
  {
    g_display_state.wifi_connected = connected;
    xSemaphoreGive(g_display_mutex);
  }
}

static void display_state_set_mqtt(bool connected)
{
  if (xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(50)))
  {
    g_display_state.mqtt_connected = connected;
    xSemaphoreGive(g_display_mutex);
  }
}

static void display_state_update_gps(const gps_data_ready_payload_t *gps)
{
  if (!gps) return;

  if (xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(50)))
  {
    g_display_state.latitude = gps->latitude;
    g_display_state.longitude = gps->longitude;
    g_display_state.altitude = gps->altitude;

    g_display_state.speed_kmh = gps->speed_kmh;
    g_display_state.course_deg = gps->course_deg;

    g_display_state.satellites = gps->satellites;
    g_display_state.hdop = gps->hdop;

    g_display_state.gps_timestamp = gps->timestamp;

    xSemaphoreGive(g_display_mutex);
  }
}

static display_state_t display_state_snapshot(void)
{
  display_state_t copy = {0};

  if (xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(50)))
  {
    copy = g_display_state;
    xSemaphoreGive(g_display_mutex);
  }

  return copy;
}

/* ========================================================================= */
/* Initialization                                                          */
/* ========================================================================= */

esp_err_t display_initialization(void)
{
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << OLED_RESET_PIN),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE};

  ESP_ERROR_CHECK(gpio_config(&io_conf));

  gpio_set_level(OLED_RESET_PIN, 0);
  vTaskDelay(pdMS_TO_TICKS(50));
  gpio_set_level(OLED_RESET_PIN, 1);
  vTaskDelay(pdMS_TO_TICKS(50));

  g_display_mutex = xSemaphoreCreateMutex();
  if (!g_display_mutex)
  {
    ESP_LOGE(APP_TAG, "Mutex creation failed");
    return ESP_FAIL;
  }

  i2c_master_bus_config_t bus_config = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = I2C_NUM_0,
      .scl_io_num = I2C_SCL_PIN,
      .sda_io_num = I2C_SDA_PIN,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true};

  ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c0_bus_hdl));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL, NULL));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL, NULL));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      GPS_EVENT, GPS_EVENT_DATA_READY, event_handler, NULL, NULL));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      SYSTEM_API, ESP_EVENT_ANY_ID, event_handler, NULL, NULL));

  xTaskCreate(i2c0_ssd1306_task, "display_task", 4096, NULL, 5, NULL);

  ESP_LOGI(APP_TAG, "Display initialized");

  return ESP_OK;
}

/* ========================================================================= */
/* Display Task                                                            */
/* ========================================================================= */

static void i2c0_ssd1306_task(void *pvParameters)
{
  ssd1306_config_t dev_cfg = I2C_SSD1306_128x64_CONFIG_DEFAULT;
  ssd1306_handle_t dev_hdl;

  if (ssd1306_init(i2c0_bus_hdl, &dev_cfg, &dev_hdl) != ESP_OK)
  {
    ESP_LOGE(APP_TAG, "SSD1306 init failed");
    vTaskDelete(NULL);
    return;
  }

  char line[17];

  while (true)
  {
    display_state_t state = display_state_snapshot();

    time_t now;
    time(&now);

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    bool gps_online = (state.gps_timestamp > 0) &&
                      ((now - state.gps_timestamp) < 30);

    ssd1306_clear_display(dev_hdl, false);
    ssd1306_set_contrast(dev_hdl, 0xFF);

    /* LINE 0 */
    snprintf(line, sizeof(line),
             "%02d:%02d S%02lu %s",
             timeinfo.tm_hour,
             timeinfo.tm_min,
             (unsigned long)state.satellites,
             state.wifi_connected ? "ON " : "OFF");

    ssd1306_display_text(dev_hdl, 0, line, false);

    /* LINE 1 */
    ssd1306_display_text(dev_hdl, 1, "----------------", false);

    /* LINE 2 */
    snprintf(line, sizeof(line),
             "Lat: %c%09.5f",
             state.latitude >= 0 ? '+' : '-',
             fabs(state.latitude));

    ssd1306_display_text(dev_hdl, 2, line, false);

    /* LINE 3 */
    snprintf(line, sizeof(line),
             "Lon: %c%09.5f",
             state.longitude >= 0 ? '+' : '-',
             fabs(state.longitude));

    ssd1306_display_text(dev_hdl, 3, line, false);

    /* LINE 4 */
    if (gps_online)
    {
      snprintf(line, sizeof(line),
               "Sat:     %02lu",
               (unsigned long)state.satellites);
    }
    else
    {
      snprintf(line, sizeof(line),
               "Sat:      Lost");
    }

    ssd1306_display_text(dev_hdl, 4, line, false);

    /* LINE 5 */
    snprintf(line, sizeof(line),
             "  %06.2f km/h",
             state.speed_kmh);

    ssd1306_display_text(dev_hdl, 5, line, false);

    /* LINE 6 */
    ssd1306_display_text(dev_hdl, 6, "----------------", false);

    /* LINE 7 MQTT */
    if (state.mqtt_connected)
    {
      ssd1306_display_text(dev_hdl, 7, "MQTT: ONLINE ", false);
    }
    else
    {
      ssd1306_display_text(dev_hdl, 7, "MQTT: OFFLINE", false);
    }

    vTaskDelay(pdMS_TO_TICKS(DISPLAY_REFRESH_SECONDS * 1000));
  }
}

/* ========================================================================= */
/* Event Handler                                                          */
/* ========================================================================= */

static void event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data)
{
  /* WIFI */
  if (base == WIFI_EVENT)
  {
    if (event_id == WIFI_EVENT_STA_START ||
        event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
      display_state_set_wifi(false);
    }
    return;
  }

  /* IP */
  if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
  {
    display_state_set_wifi(true);
    return;
  }

  /* GPS */
  if (base == GPS_EVENT && event_id == GPS_EVENT_DATA_READY)
  {
    display_state_update_gps(
        (gps_data_ready_payload_t *)event_data);
    return;
  }

  /* SYSTEM / MQTT */
  if (base == SYSTEM_API)
  {
    if (event_id == SYSTEM_API_EVENT_CONNECTED)
    {
      display_state_set_mqtt(true);
    }
    else if (event_id == SYSTEM_API_EVENT_DISCONNECTED)
    {
      display_state_set_mqtt(false);
    }
    return;
  }
}