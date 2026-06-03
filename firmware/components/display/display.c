#include "display.h"
#include "ssd1306.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include <time.h>
#include <math.h> // Necessário para a função fabsf()

#define I2C_SCL_PIN 15
#define I2C_SDA_PIN 4
#define OLED_RESET_PIN 16

#define I2C0_TASK_SAMPLING_RATE 5
#define APP_TAG "MAIN"

i2c_master_bus_handle_t i2c0_bus_hdl;

void i2c0_ssd1306_task(void *pvParameters);

static void event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

esp_err_t display_initialization()
{
  // 1. Reset físico do OLED via GPIO
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << OLED_RESET_PIN),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&io_conf);

  gpio_set_level(OLED_RESET_PIN, 0);
  vTaskDelay(pdMS_TO_TICKS(50));
  gpio_set_level(OLED_RESET_PIN, 1);
  vTaskDelay(pdMS_TO_TICKS(50));

  // 2. Configuração do I2C
  i2c_master_bus_config_t bus_config = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = I2C_NUM_0,
      .scl_io_num = I2C_SCL_PIN,
      .sda_io_num = I2C_SDA_PIN,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };

  esp_err_t ret = i2c_new_master_bus(&bus_config, &i2c0_bus_hdl);

  if (ret != ESP_OK)
  {
    ESP_LOGE(APP_TAG, "Falha crítica na inicialização do I2C/Display: %s", esp_err_to_name(ret));
    return ESP_FAIL;
  }

  ESP_LOGI(APP_TAG, "I2C e Display inicializados com sucesso.");

  // 3. Cria a Task no FreeRTOS para rodar o display
  xTaskCreate(
      i2c0_ssd1306_task,   
      "i2c0_ssd1306_task", 
      2048 * 2,            
      NULL,                
      5,                   
      NULL                 
  );

  return ESP_OK;
}

void i2c0_ssd1306_task(void *pvParameters)
{
  ssd1306_config_t dev_cfg = I2C_SSD1306_128x64_CONFIG_DEFAULT;
  ssd1306_handle_t dev_hdl;

  // Inicializa o display
  esp_err_t err = ssd1306_init(i2c0_bus_hdl, &dev_cfg, &dev_hdl);
  if (err != ESP_OK || dev_hdl == NULL)
  {
    ESP_LOGE(APP_TAG, "Erro ao inicializar SSD1306");
    vTaskDelete(NULL);
    return;
  }

  // Variáveis dinâmicas (Esses valores mudarão na sua aplicação real)
  float latitude = -23.55052;
  float longitude = -46.63330;
  float velocidade = 5.32; 
  bool mqtt_conectado = true;

  char buffer_linha[17]; // 16 caracteres + \0
  time_t now;
  struct tm timeinfo; 

  for (;;)
  {
    // Limpa o display
    ssd1306_clear_display(dev_hdl, false);
    ssd1306_set_contrast(dev_hdl, 0xff);

    // --- LINHA 0: Status e Hora (Fixo 16 caracteres) ---
    time(&now);
    localtime_r(&now, &timeinfo);
    snprintf(buffer_linha, sizeof(buffer_linha), "Online     %02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    ssd1306_display_text(dev_hdl, 0, buffer_linha, false);

    // --- LINHA 1: Separador ---
    ssd1306_display_text(dev_hdl, 1, "----------------", false);

    // --- LINHA 2 e 3: Latitude e Longitude Dinâmicas ---
    // Tratamos o sinal separadamente para garantir que o preenchimento de zeros preencha 
    // corretamente a parte numérica (3 dígitos para graus + 1 ponto + 5 decimais = 9 caracteres)
    char sinal_lat = (latitude >= 0) ? '+' : '-';
    snprintf(buffer_linha, sizeof(buffer_linha), "Lat:  %c%09.5f", sinal_lat, fabsf(latitude));
    ssd1306_display_text(dev_hdl, 2, buffer_linha, false);
    // Resultado ex: "Lat:  -023.55052" ou "Lat:  +005.12345" -> Exatamente 16 caracteres!

    char sinal_long = (longitude >= 0) ? '+' : '-';
    snprintf(buffer_linha, sizeof(buffer_linha), "Long: %c%09.5f", sinal_long, fabsf(longitude));
    ssd1306_display_text(dev_hdl, 3, buffer_linha, false);
    // Resultado ex: "Long: -046.63330" ou "Long: +000.01234" -> Exatamente 16 caracteres!
    
    // --- LINHA 5: Velocidade (Centralizada e Segura) ---
    // %06.2f garante 3 dígitos inteiros + ponto + 2 decimais (Ex: 005.32 ou 120.45)
    // "  %06.2f km/h " -> 2 espaços + 6 caracteres + 5 caracteres (" km/h") + 3 espaços = 16
    // Caso a velocidade passe de 100, os espaços vazios se ajustam
    if (velocidade < 100.0f) {
        snprintf(buffer_linha, sizeof(buffer_linha), "   %05.2f km/h   ", velocidade);
    } else {
        snprintf(buffer_linha, sizeof(buffer_linha), "  %06.2f km/h   ", velocidade);
    }
    ssd1306_display_text(dev_hdl, 5, buffer_linha, false);

    // --- LINHA 6: Separador Inferior ---
    ssd1306_display_text(dev_hdl, 6, "----------------", false);

    // --- LINHA 7: Status do MQTT (Alinhado com espaços no final para somar 16) ---
    if (mqtt_conectado)
    {
      ssd1306_display_text(dev_hdl, 7, "MQTT: CONNECTED ", false); // 16 caracteres
    }
    else
    {
      ssd1306_display_text(dev_hdl, 7, "MQTT: DISCONNECT", false); // 16 caracteres
    }

    // Tempo de atualização (5 segundos)
    vTaskDelay(pdMS_TO_TICKS(I2C0_TASK_SAMPLING_RATE * 1000));
  }

  ssd1306_delete(dev_hdl);
  vTaskDelete(NULL);
}

static void gps_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
}