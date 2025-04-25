#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/time.h>

#define PORT 9877
#define BUFLEN 512
#define MAX_RETRIES 5
#define TMIN 500

// PowerUDP header
typedef struct {
    uint32_t seq_num;    // Número de sequência
    uint8_t ack;         // Flag: 1 se for ACK, 0 se for dados
    uint8_t flags;       // Podes definir outros bits de controlo
    uint16_t length;     // Tamanho do payload
} PowerUDPHeader;

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

        // Exponential backoff
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
        envia_acknak(sockfd, &src, header.seq_num, 2); // NAK
        return;
    }

    envia_acknak(sockfd, &src, header.seq_num, 1); // ACK

    memcpy(dados, buffer + sizeof(header), header.length);
    dados[header.length] = '\0';

    printf("Recebido seq=%u: %s\n", header.seq_num, dados);
    expected_seq++;
}

void envia_acknak(int sockfd, struct sockaddr_in *dest, uint32_t seq_num, uint8_t tipo) {
    PowerUDPHeader header;
    header.seq_num = htonl(seq_num);
    header.ack = tipo; // 1 = ACK, 2 = NAK
    header.flags = 0;
    header.length = 0;

    sendto(sockfd, &header, sizeof(header), 0, (struct sockaddr *)dest, sizeof(*dest));
}

// Função de erro
void erro(char *s) {
    perror(s);
    exit(1);
}

int main() {
    int sockfd;
    struct sockaddr_in addr;
    uint32_t seq_num = 0;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        perror("socket");
        exit(1);
    }

    // Bind (para também receber mensagens)
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sockfd);
        exit(1);
    }

    // Endereço de destino
    struct sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_port = htons(PORT);
    dest.sin_addr.s_addr = inet_addr("127.0.0.1");

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(0, &readfds);      // stdin
        FD_SET(sockfd, &readfds); // socket

        select(sockfd + 1, &readfds, NULL, NULL, NULL);

        if (FD_ISSET(0, &readfds)) {
            char buf[BUFLEN];
            fgets(buf, BUFLEN, stdin);
            buf[strcspn(buf, "\n")] = 0;

            envia_powerudp_confiavel(sockfd, &dest, buf, seq_num++);
        }

        if (FD_ISSET(sockfd, &readfds)) {
            recebe_powerudp_com_ack(sockfd);
        }
    }

    close(sockfd);
    return 0;
}