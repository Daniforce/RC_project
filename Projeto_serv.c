#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <arpa/inet.h>

#define SERVER_PORT     443
#define BUF_SIZE        1024

struct RegisterMessage {
    char psk[64];
};

struct ConfigMessage {
    uint8_t enable_retransmission;
    uint8_t enable_backoff;
    uint8_t enable_sequence;
    uint16_t base_timeout;  
    uint8_t max_retries;
};

struct ConfigMessage configuracao_ativa = {
    .enable_retransmission = 1,
    .enable_backoff = 1,
    .enable_sequence = 1,
    .base_timeout = 0,
    .max_retries = 5
};

struct ClienteInfo {
    struct in_addr ip;
};

struct ClienteInfo clientes[3];
int num_clientes = 0;
void process_client(int client_fd, struct sockaddr_in *client_addr);
void enviar_config_multicast();
void erro(char *msg);
void adicionar_cliente(struct in_addr ip);

int main() 
{
    int fd, client;
    struct sockaddr_in addr, client_addr;
    int client_addr_size;
    configuracao_ativa.base_timeout = htons(200);
    client_addr_size = sizeof(client_addr);

    bzero((void *) &addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(SERVER_PORT);

    if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        erro("na funcao socket");
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        erro("na funcao bind");
    if (listen(fd, 5) < 0)
        erro("na funcao listen");

    while (1)
    {
        while (waitpid(-1, NULL, WNOHANG) > 0);

        if((client = accept(fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_size)) == -1)
          erro("na função accept");

        if (client > 0) {
            if (fork() == 0) {
                close(fd);
                process_client(client, &client_addr);
                exit(0);
            }
            close(client);
        }
    }
    return 0;
}

void process_client(int client_fd, struct sockaddr_in *client_addr)
{
    struct RegisterMessage reg;
    ssize_t nread;
    const char *valid_psk = "my_secret_key";
    const char *ACK = "ACK";

    nread = read(client_fd, &reg, sizeof(reg));
    if (nread != sizeof(reg)) {
        fprintf(stderr, "[ERRO] Erro ao ler RegisterMessage\n");
        close(client_fd);
        return;
    }

    reg.psk[63] = '\0';

    if (strcmp(reg.psk, valid_psk) != 0) {
        fprintf(stderr, "[WARN] PSK inválida: %s\n", reg.psk);
        const char *NAK = "NAK";
        write(client_fd, NAK, strlen(NAK));
        close(client_fd);
        return;
    }

    write(client_fd, ACK, strlen(ACK));

    adicionar_cliente(client_addr->sin_addr);

    enviar_config_multicast();  

    while (1) 
    {
        struct ConfigMessage req;
        ssize_t r = read(client_fd, &req, sizeof(req));
        if (r == 0) {
            printf("[INFO] Cliente fechou a ligação\n");
            break;
        } else if (r != sizeof(req)) {
            fprintf(stderr, "[WARN] Erro ou mensagem malformada recebida\n");
            break;
        }

        configuracao_ativa.enable_retransmission = req.enable_retransmission;
        configuracao_ativa.enable_backoff = req.enable_backoff;
        configuracao_ativa.enable_sequence = req.enable_sequence;
        configuracao_ativa.base_timeout = req.base_timeout;
        configuracao_ativa.max_retries = req.max_retries;

        printf("[INFO] Nova configuração recebida de %s\n", inet_ntoa(client_addr->sin_addr));
        enviar_config_multicast();  
    }

    close(client_fd);
}

void enviar_config_multicast() {
    int sockfd;
    struct sockaddr_in dest;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket multicast");
        return;
    }

    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(9876); 
    inet_pton(AF_INET, "239.0.0.1", &dest.sin_addr);

    if (sendto(sockfd, &configuracao_ativa, sizeof(configuracao_ativa), 0,
               (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        perror("sendto multicast");
    } else {
        printf("[INFO] Configuração enviada via multicast.\n");
    }

    close(sockfd);
}


void erro(char *msg)
{
    perror(msg);
    exit(1);
}

void adicionar_cliente(struct in_addr ip) {
    for (int i = 0; i < num_clientes; i++) {
        if (clientes[i].ip.s_addr == ip.s_addr) return;
    }

    if (num_clientes < 3) {
        clientes[num_clientes++].ip = ip;
        printf("[INFO] Cliente adicionado: %s\n", inet_ntoa(ip));
    } else {
        printf("[WARN] Número máximo de clientes atingido.\n");
    }
}