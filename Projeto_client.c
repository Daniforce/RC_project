#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>

#define PORT 9877
#define BUFLEN 512

// PowerUDP header
typedef struct {
    uint32_t seq_num;    // Número de sequência
    uint8_t ack;         // Flag: 1 se for ACK, 0 se for dados
    uint8_t flags;       // Podes definir outros bits de controlo
    uint16_t length;     // Tamanho do payload
} PowerUDPHeader;

// Função para enviar pacotes PowerUDP
void envia_powerudp(int sockfd, struct sockaddr_in *dest, const char *dados, uint32_t seq_num) {
    PowerUDPHeader header;
    header.seq_num = htonl(seq_num);
    header.ack = 0;
    header.flags = 0;
    header.length = htons(strlen(dados));

    char buffer[1024];
    memcpy(buffer, &header, sizeof(header));
    memcpy(buffer + sizeof(header), dados, strlen(dados));

    sendto(sockfd, buffer, sizeof(header) + strlen(dados), 0, (struct sockaddr *)dest, sizeof(*dest));
}

// Função para receber pacotes PowerUDP
void recebe_powerudp(int sockfd) {
    char buffer[1024];
    struct sockaddr_in src;
    socklen_t srclen = sizeof(src);

    ssize_t len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&src, &srclen);

    PowerUDPHeader header;
    memcpy(&header, buffer, sizeof(header));

    header.seq_num = ntohl(header.seq_num);
    header.length = ntohs(header.length);

    char dados[1024];
    memcpy(dados, buffer + sizeof(header), header.length);
    dados[header.length] = '\0';

    printf("Recebido seq=%u: %s\n", header.seq_num, dados);
}

// Função de erro
void erro(char *s) {
    perror(s);
    exit(1);
}

int main() {
    // Definição de algumas variáveis necessárias
    char buf[BUFLEN], resposta_bin[BUFLEN], resposta_hex[BUFLEN];
    int s, recv_len_bin, recv_len_hex;
    struct sockaddr_in addr;
    socklen_t slen = sizeof(addr);
    static uint32_t seq_num = 0; // Variável para número de sequência

    // Cria um socket para pacotes UDP
    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        erro("Erro ao criar socket");

    // Preenche o socket address structure
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Endereço do servidor (localhost)

    // Recebe a entrada do usuário
    fgets(buf, BUFLEN, stdin);
    buf[strcspn(buf, "\n")] = 0; // Remove o caractere de nova linha

    // Envia a mensagem com PowerUDP
    envia_powerudp(s, &addr, buf, seq_num++);
    
    // Resposta do servidor (binário)
    if ((recv_len_bin = recvfrom(s, resposta_bin, BUFLEN, 0, (struct sockaddr *)&addr, &slen)) == -1)
        erro("recvfrom");

    // Recebe e processa a resposta usando PowerUDP
    recebe_powerudp(s);

    // Fecha socket e encerra o programa
    close(s);
    return 0;
}
