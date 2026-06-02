#include "gps.h"
#include "TinyGPSPlus.h" // Cabeçalho da biblioteca adicionada pelo gerenciador
#include "driver/uart.h" // Driver de UART nativo do ESP-IDF

esp_err_t gps_initialization()
{
  return ESP_OK;
}

// Instância estática do objeto C++ (escondida do resto do projeto em C)
static TinyGPSPlus gps;
static uart_port_t g_uart_num;

void meu_gps_init(int uart_num, int tx_pin, int rx_pin, int baud_rate)
{
  g_uart_num = (uart_port_t)uart_num;

  uart_config_t uart_config = {
      .baud_rate = baud_rate,
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

  // 1. Instala o driver primeiro
  ESP_ERROR_CHECK(uart_driver_install(g_uart_num, 1024 * 2, 0, 0, NULL, 0));
  // 2. Configura os parâmetros
  ESP_ERROR_CHECK(uart_param_config(g_uart_num, &uart_config));
  // 3. Configura os pinos
  ESP_ERROR_CHECK(uart_set_pin(g_uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

void meu_gps_processar(void)
{
  uint8_t dado[128];
  // Lê os bytes brutos da UART do SAM-M10Q
  int tamanho = uart_read_bytes(g_uart_num, dado, sizeof(dado), 20 / portTICK_PERIOD_MS);

  if (tamanho > 0)
  {
    // Alimenta o objeto C++ byte a byte
    for (int i = 0; i < tamanho; i++)
    {
      gps.encode(dado[i]);
    }
  }
}

void meu_gps_obter_dados(dados_gps_t *dados)
{
  if (dados == NULL)
    return;

  dados->localizacao_valida = gps.location.isValid();

  if (dados->localizacao_valida)
  {
    dados->latitude = gps.location.lat();
    dados->longitude = gps.location.lng();
  }

  dados->altitude = gps.altitude.meters();
  dados->satelites = gps.satellites.value();

  if (gps.speed.isValid())
  {
    dados->velocidade_kmh = gps.speed.kmph();
  }
  else
  {
    dados->velocidade_kmh = 0.0;
  }

  // Extrai a direção em graus
  if (gps.course.isValid())
  {
    dados->direcao_graus = gps.course.deg();
  }
  else
  {
    dados->direcao_graus = 0.0;
  }
}

// Implementação da conversão de graus para pontos cardeais/colaterais
const char *meu_gps_graus_para_cardeal(double graus)
{
  static const char *direcoes[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW", "N"};
  // Divide os 360 graus em 8 fatias de 45 graus cada
  int index = (int)((graus + 22.5) / 45.0);
  return direcoes[index % 8];
}