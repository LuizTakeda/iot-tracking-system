#include "command_interface.h"
#include "command_interface_events.h"

#include <string.h>

#include "driver/uart.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define COMMAND_BUFFER_SIZE 128
#define COMMAND_UART_PORT UART_NUM_0

ESP_EVENT_DEFINE_BASE(COMMAND_INTERFACE_EVENTS);

//**************************************************
// Private Constants
//**************************************************

static const char *TAG = "command_interface";

//**************************************************
// Private Function Prototypes
//**************************************************

/**
 * @brief Task responsible for receiving and parsing terminal commands.
 *
 * Supported commands:
 * - wifi,<SSID>,<PASSWORD>
 *
 * @param pvParameters Unused task parameter.
 */
static void command_interface_terminal_task(void *pvParameters);

/**
 * @brief Parses and executes a received command.
 *
 * @param command Null-terminated command string.
 */
static void process_command(char *command);

/**
 * @brief Resets the command buffer state.
 *
 * @param buffer Command buffer.
 * @param index Pointer to the current buffer index.
 */
static void reset_command_buffer(char *buffer, size_t *index);

/**
 * @brief Posts Wi-Fi credentials to the event loop.
 *
 * @param ssid Wi-Fi SSID.
 * @param password Wi-Fi password.
 *
 * @return
 * - ESP_OK on success.
 * - Error code returned by esp_event_post().
 */
static esp_err_t post_wifi_credentials(const char *ssid, const char *password);

//**************************************************
// Public Functions
//**************************************************

/**
 * @brief Initializes the command interface module.
 * * Sets up the default event loop, configures UART 0, installs the driver,
 * and spawns the terminal reader FreeRTOS task.
 * * @return
 * - ESP_OK on success.
 * - ESP_FAIL if task creation fails.
 * - Error code from event loop or UART initialization APIs.
 */
esp_err_t command_interface_initialization(void)
{
  esp_err_t err = esp_event_loop_create_default();

  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
  {
    ESP_LOGE(TAG, "Failed to create default event loop");
    return err;
  }

  // 1. Configure UART 0 parameters
  uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  // 2. Install the driver (UART_NUM_0 uses the default flashing/logging pins)
  // Allocate an RX ring buffer (rx_buffer_size = 256 bytes)
  err = uart_driver_install(COMMAND_UART_PORT, 256, 0, 0, NULL, 0);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to install UART driver");
    return err;
  }

  err = uart_param_config(COMMAND_UART_PORT, &uart_config);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to configure UART parameters");
    return err;
  }

  uart_set_pin(COMMAND_UART_PORT, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

  // 3. Create the FreeRTOS Task
  BaseType_t task_created = xTaskCreate(
      command_interface_terminal_task,
      "command_interface_terminal_task",
      4096,
      NULL,
      5,
      NULL);

  if (task_created != pdPASS)
  {
    ESP_LOGE(TAG, "Failed to create terminal parser task");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Command interface initialized successfully");

  return ESP_OK;
}

//**************************************************
// Private Functions
//**************************************************

static void command_interface_terminal_task(void *pvParameters)
{
  (void)pvParameters;

  char input_buffer[COMMAND_BUFFER_SIZE] = {0};
  size_t buffer_index = 0;

  ESP_LOGI(TAG, "Terminal parser task started");

  while (true)
  {
    uint8_t character;

    // Read 1 byte from UART with an infinite timeout
    int bytes_read = uart_read_bytes(COMMAND_UART_PORT, &character, 1, portMAX_DELAY);

    if (bytes_read <= 0)
    {
      continue;
    }

    // Check for carriage return or line feed (end of command string)
    if (character != '\n' && character != '\r')
    {
      // Check for buffer overflow before appending
      if (buffer_index >= (COMMAND_BUFFER_SIZE - 1))
      {
        ESP_LOGW(TAG, "Command buffer overflow detected. Discarding current input");

        reset_command_buffer(input_buffer, &buffer_index);

        continue;
      }

      input_buffer[buffer_index++] = (char)character;
      continue;
    }

    // Ignore empty lines/commands
    if (buffer_index == 0)
    {
      continue;
    }

    input_buffer[buffer_index] = '\0';

    process_command(input_buffer);

    reset_command_buffer(input_buffer, &buffer_index);
  }
}

static void process_command(char *command)
{
  // Tokenize string using comma as delimiter
  char *command_name = strtok(command, ",");
  char *arg1 = strtok(NULL, ",");
  char *arg2 = strtok(NULL, ",");

  if (command_name == NULL)
  {
    return;
  }

  // Handle "wifi" command
  if (strcmp(command_name, "wifi") == 0)
  {
    if (arg1 == NULL || arg2 == NULL)
    {
      ESP_LOGW(TAG, "Invalid Wi-Fi command. Expected format: wifi,<SSID>,<PASSWORD>");
      return;
    }

    ESP_LOGI(TAG, "Received Wi-Fi configuration command (SSID='%s')", arg1);

    esp_err_t err = post_wifi_credentials(arg1, arg2);

    if (err != ESP_OK)
    {
      ESP_LOGE(TAG, "Failed to post Wi-Fi credentials event (err=0x%x)", err);
    }

    return;
  }

  ESP_LOGW(TAG, "Unknown command '%s'", command_name);
}

static void reset_command_buffer(char *buffer, size_t *index)
{
  *index = 0;
  memset(buffer, 0, COMMAND_BUFFER_SIZE);
}

static esp_err_t post_wifi_credentials(const char *ssid, const char *password)
{
  command_interface_wifi_credentials_payload_t payload;

  memset(&payload, 0, sizeof(payload));

  // Safely copy SSID and password, ensuring room for the null-terminator
  strncpy(payload.ssid, ssid, sizeof(payload.ssid) - 1);
  strncpy(payload.password, password, sizeof(payload.password) - 1);

  ESP_LOGI(TAG, "Posting Wi-Fi credentials event for SSID '%s'", payload.ssid);

  return esp_event_post(
      COMMAND_INTERFACE_EVENTS,
      CMD_EVENT_WIFI_CREDENTIALS,
      &payload,
      sizeof(payload),
      portMAX_DELAY);
}