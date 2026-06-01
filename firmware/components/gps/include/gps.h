#ifndef MEU_GPS_H
#define MEU_GPS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

// Estrutura simples em C para exportar os dados que você quer
typedef struct {
    double latitude;
    double longitude;
    double altitude;
    bool localizacao_valida;
    int satelites;
    double velocidade_kmh;
    double direcao_graus;    // <-- Novo campo
} dados_gps_t;

/* --- FUNÇÕES EXPORTADAS EM C --- */

// Inicializa a UART do ESP32 para o GPS
void meu_gps_init(int uart_num, int tx_pin, int rx_pin, int baud_rate);

// Processa os bytes vindos da UART e atualiza o parser
void meu_gps_processar(void);

// Copia os dados convertidos para a estrutura em C
void meu_gps_obter_dados(dados_gps_t *dados);

const char* meu_gps_graus_para_cardeal(double graus);

#ifdef __cplusplus
}
#endif

#endif // MEU_GPS_H