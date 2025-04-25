#include "POWERUDP_H.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>

// Variáveis globais para simulação básica
static int simulated_loss = 0;
static int last_retransmissions = 0;
static int last_delivery_time = 0;

int init_protocol(const char *server_ip, int server_port, const char *psk) {
    // Aqui poderias abrir um socket e fazer handshake com o servidor PowerUDP
    printf("[init_protocol] IP: %s | Porta: %d | PSK: %s\n", server_ip, server_port, psk);
    return 0;
}

void close_protocol() {
    printf("[close_protocol] Fechando protocolo.\n");
}

int request_protocol_config(int enable_retransmission, int enable_backoff, int enable_sequence, uint16_t base_timeout, uint8_t max_retries) {
    printf("[request_protocol_config] retrans=%d backoff=%d seq=%d timeout=%d retries=%d\n",
           enable_retransmission, enable_backoff, enable_sequence, base_timeout, max_retries);
    return 0;
}

int send_message(const char *destination, const char *message, int len) {
    // Simulação de perda
    int drop = rand() % 100;
    if (drop < simulated_loss) {
        printf("[send_message] Mensagem PERDIDA (simulação).\n");
        last_retransmissions = 1;
        last_delivery_time = 0;
        return -1;
    }

    printf("[send_message] Enviando para %s: %s\n", destination, message);
    last_retransmissions = 0;
    last_delivery_time = rand() % 100 + 50;  // Simula tempo entre 50-150ms
    return 0;
}

int receive_message(char *buffer, int bufsize) {
    // Aqui implementarias a receção com um socket
    return 0;
}

int get_last_message_stats(int *retransmissions, int *delivery_time) {
    *retransmissions = last_retransmissions;
    *delivery_time = last_delivery_time;
    return 0;
}

void inject_packet_loss(int probability) {
    simulated_loss = probability;
    printf("[inject_packet_loss] Perda simulada: %d%%\n", simulated_loss);
}
