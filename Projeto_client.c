#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include "POWERUDP_H.h"

#define PORT 9877
#define BUFLEN 512
#define MAX_RETRIES 5
#define TMIN 500

#define MULTICAST_GROUP "239.0.0.1"
#define MULTICAST_PORT 9876

int multicast_sock;

typedef struct {
    uint32_t seq_num;    
    uint8_t ack;         
    uint8_t flags;      
    uint16_t length;    
} PowerUDPHeader;

typedef struct 
{
    uint8_t enable_retransmission;  
    uint8_t enable_backoff;         
    uint8_t enable_sequence;     
    uint16_t base_timeout;         
    uint8_t max_retries;      
} ConfigMessage;

void configurar_socket_multicast() {
    struct sockaddr_in addr;
    struct ip_mreq mreq;

    multicast_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (multicast_sock < 0) {
        perror("socket multicast");
        exit(1);
    }

    int reuse = 1;
    if (setsockopt(multicast_sock, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        exit(1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(MULTICAST_PORT);

    if (bind(multicast_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind multicast");
        exit(1);
    }

    mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_GROUP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(multicast_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt IP_ADD_MEMBERSHIP");
        exit(1);
    }

    printf("[INFO] Socket multicast configurado.\n");
}

int ler_header(int sockfd, char *buffer, struct sockaddr_in *src, socklen_t *addrlen, PowerUDPHeader *header) {
    ssize_t len = recvfrom(sockfd, buffer, 1024, 0, (struct sockaddr *)src, addrlen);
    if (len < sizeof(PowerUDPHeader)) return -1;
    
    memcpy(header, buffer, sizeof(PowerUDPHeader));
    header->seq_num = ntohl(header->seq_num);
    header->length = ntohs(header->length);
    return len;
}

void envia_powerudp(int sockfd, struct sockaddr_in *dest, const char *dados, uint32_t seq_num) {
    PowerUDPHeader header;
    header.seq_num = htonl(seq_num);
    header.ack = 0;
    header.flags = 0;
    header.length = htons(strlen(dados));

    char buffer[BUFLEN];
    memcpy(buffer, &header, sizeof(header));
    memcpy(buffer + sizeof(header), dados, strlen(dados));

    sendto(sockfd, buffer, sizeof(header) + strlen(dados), 0, (struct sockaddr *)dest, sizeof(*dest));
}

void envia_powerudp_confiavel(int sockfd, struct sockaddr_in *dest, const char *dados, uint32_t seq_num) {
    int tentativas = 0;
    int ack_recebido = 0;
    socklen_t addrlen = sizeof(*dest);

    struct timeval timeout;

    while (tentativas < MAX_RETRIES && !ack_recebido) {
        envia_powerudp(sockfd, dest, dados, seq_num);

        timeout.tv_sec = 0;
        timeout.tv_usec = TMIN * 1000 * (1 << tentativas);
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        char buffer[1024];
        struct sockaddr_in src;
        PowerUDPHeader header;

        ssize_t len = ler_header(sockfd, buffer, &src, &addrlen, &header);

        if (len > 0) {
            if (header.ack == 1 && header.seq_num == seq_num) {
                printf("ACK recebido para seq=%u\n", seq_num);
                ack_recebido = 1;
            } else if (header.ack == 2) {
                printf("NAK recebido! Reenviando...\n");
            }
        } else {
            printf("Timeout: tentativa %d\n", tentativas + 1);
        }

        tentativas++;
    }

    if (!ack_recebido) {
        printf("Erro: não foi possível confirmar entrega após %d tentativas\n", MAX_RETRIES);
    }
}

void envia_acknak(int sockfd, struct sockaddr_in *dest, uint32_t seq_num, uint8_t tipo);


void recebe_powerudp_com_ack(int sockfd) {
    char buffer[BUFLEN], dados[BUFLEN];;
    struct sockaddr_in src;
    PowerUDPHeader header;
    socklen_t srclen = sizeof(src);
    static uint32_t expected_seq = 0;

    ssize_t len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&src, &srclen);

    memcpy(&header, buffer, sizeof(header));
    header.seq_num = ntohl(header.seq_num);
    header.length = ntohs(header.length);

    if (header.seq_num != expected_seq) {
        printf("Fora de ordem. Esperado: %u, recebido: %u\n", expected_seq, header.seq_num);
        envia_acknak(sockfd, &src, header.seq_num, 2); 
        return;
    }

    envia_acknak(sockfd, &src, header.seq_num, 1);

    memcpy(dados, buffer + sizeof(header), header.length);
    dados[header.length] = '\0';

    printf("Recebido seq=%u: %s\n", header.seq_num, dados);
    expected_seq++;
}

void envia_acknak(int sockfd, struct sockaddr_in *dest, uint32_t seq_num, uint8_t tipo) {
    PowerUDPHeader header;
    header.seq_num = htonl(seq_num);
    header.ack = tipo; 
    header.flags = 0;
    header.length = 0;

    sendto(sockfd, &header, sizeof(header), 0, (struct sockaddr *)dest, sizeof(*dest));
}

void erro(char *s) {
    perror(s);
    exit(1);
}

int main() {
    int sockfd;
    struct sockaddr_in addr, dest;
    
    uint32_t seq_num = 0;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(PORT);
    dest.sin_addr.s_addr = htonl(INADDR_ANY);

    if (connect(sockfd, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        perror("bind");
        close(sockfd);
        exit(1);
    }

    dest.sin_family = AF_INET;
    dest.sin_port = htons(PORT);
    dest.sin_addr.s_addr = inet_addr("127.0.0.1");

    write(sockfd, POWERUDP_PSK, strlen(POWERUDP_PSK));

          char resposta[4];
    read(sockfd, resposta, sizeof(resposta));
    resposta[3] = '\0';

    if (strcmp(resposta, "ACK") == 0) {
        printf("Autenticado! Iniciando comunicação...\n");
        fd_set readfds;
        int maxfd = (sockfd > multicast_sock ? sockfd : multicast_sock);
        configurar_socket_multicast();

        while (1) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sockfd, &readfds);          
            FD_SET(multicast_sock, &readfds);

            int maxfd = sockfd;
            if (multicast_sock > maxfd) maxfd = multicast_sock;

            if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0) {
                perror("select");
                continue;
            }

            if (FD_ISSET(0, &readfds)) {
                char buf[BUFLEN];
                fgets(buf, BUFLEN, stdin);
                buf[strcspn(buf, "\n")] = 0;

                envia_powerudp_confiavel(sockfd, &dest, buf, seq_num++);
            }

            if (FD_ISSET(sockfd, &readfds)) {
                recebe_powerudp_com_ack(sockfd);
            }

            if (FD_ISSET(multicast_sock, &readfds)) {
                ConfigMessage cfg;
                ssize_t len = recvfrom(multicast_sock, &cfg, sizeof(cfg), 0, NULL, NULL);
                if (len == sizeof(cfg)) {
                    printf("[Multicast] Nova configuração recebida:\n");
                    printf("  Retransmissão: %d\n", cfg.enable_retransmission);
                    printf("  Backoff: %d\n", cfg.enable_backoff);
                    printf("  Sequência: %d\n", cfg.enable_sequence);
                    printf("  Timeout base: %d\n", ntohs(cfg.base_timeout));
                    printf("  Retries máx: %d\n", cfg.max_retries);
            
                }
            }
        }
        close(multicast_sock);
        close(sockfd);
        return 0;
    } else {
        printf("Falha na autenticação.\n");
        close(sockfd);
        return 1;
    }
}