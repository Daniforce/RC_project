#ifndef POWERUDP_H
#define POWERUDP_H

#include <stdint.h>

int init_protocol(const char *server_ip, int server_port, const char *psk);
void close_protocol();
int request_protocol_config(int enable_retransmission, int enable_backoff, int enable_sequence, uint16_t base_timeout, uint8_t max_retries);
int send_message(const char *destination, const char *message, int len);
int receive_message(char *buffer, int bufsize);
int get_last_message_stats(int *retransmissions, int *delivery_time);
void inject_packet_loss(int probability);

#endif