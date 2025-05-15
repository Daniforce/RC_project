#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <signal.h>

#define SERVER_PORT     1048
#define BUF_SIZE        1024

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
pid_t pids[3];
int num_clientes = 0;
int client_fd_global;

void process_client(int client_fd, struct sockaddr_in *client_addr);
void enviar_config_multicast();
void adicionar_cliente(struct in_addr ip);
void handle_sigint(int sig);
void encerrar_todos_os_clientes();
void erro(char *msg);
void sigusr1_handler(int sig);
void dummy_sigusr1_handler(int sig);

int main() 
{
    int fd, client;
    struct sockaddr_in addr, client_addr;
    int client_addr_size;
    configuracao_ativa.base_timeout = htons(200);
    client_addr_size = sizeof(client_addr);

    bzero((void *) &addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    signal(SIGINT, handle_sigint);
    signal(SIGUSR1, dummy_sigusr1_handler);  // Adicionado


    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        erro("na funcao socket");
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        erro("na funcao bind");
    if (listen(fd, 3) < 0)
        erro("na funcao listen");

    while (1)
    {
        while (waitpid(-1, NULL, WNOHANG) > 0);

        if((client = accept(fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_size)) == -1)
          erro("na função accept");

        pid_t pid = fork();

        if (client > 0) {
            if (pid == 0) {
                signal(SIGINT, SIG_DFL);
                close(fd);
                process_client(client, &client_addr);
                exit(0);
            }else 
            {
                pids[num_clientes] = pid; // array global com os pids dos filhos
                adicionar_cliente(client_addr.sin_addr);
                close(client);
            }
        }
    }
    return 0;
}

void process_client(int client_fd, struct sockaddr_in *client_addr)
{
    int nread, n= 0;
    struct ConfigMessage req;
    const char *valid_psk = "my_secret_key", *ACK = "ACK";
    char psk[64];
    client_fd_global = client_fd;

    nread = read(client_fd, psk, sizeof(psk) - 1);
    if (nread <= 0) {
        fprintf(stderr, "Erro ao ler PSK\n");
        close(client_fd);
        return;
    }
    psk[nread] = '\0'; // garantir que termina em \0

    if (strcmp(psk, valid_psk) != 0) {
        fprintf(stderr, "[WARN] PSK inválida: %s\n", psk);
        const char *NAK = "NAK";
        write(client_fd, NAK, strlen(NAK));
        close(client_fd);
        return;
    }

    signal(SIGUSR1, sigusr1_handler);

    write(client_fd, ACK, strlen(ACK));

    while (1) 
    {
        ssize_t r = read(client_fd, &req, sizeof(req));
        if (r == 0) {
            printf("Cliente fechou a ligação\n");
            break;
        } else if (r != sizeof(req)) {
            fprintf(stderr, "Erro ou mensagem malformada recebida\n");
            break;
        }

        configuracao_ativa.enable_retransmission = req.enable_retransmission;
        configuracao_ativa.enable_backoff = req.enable_backoff;
        configuracao_ativa.enable_sequence = req.enable_sequence;
        configuracao_ativa.base_timeout = req.base_timeout;
        configuracao_ativa.max_retries = req.max_retries;

        if(n) {
            fprintf(stderr, "[ERRO] Erro ao ler configuração\n");
            printf("Nova configuração recebida de %s\n", inet_ntoa(client_addr->sin_addr));
            enviar_config_multicast();
        }
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

/*
**************************************************************************************
*******************************         PARA TRATAR ERROS            *********************************************
***************************************************************************************
*/

void handle_sigint(int sig) {
    printf("\nServidor a encerrar, a notificar clientes...\n");
    for (int i = 0; i < num_clientes; i++) {
        kill(pids[i], SIGUSR1);
    }
    encerrar_todos_os_clientes();
    exit(0);
}

void dummy_sigusr1_handler(int sig) {
    // Apenas para evitar mensagem "User defined signal 1"
}

void encerrar_todos_os_clientes() {
    for (int i = 0; i < num_clientes; i++) {
        int sockfd;
        struct sockaddr_in addr;

        if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("socket para fechar cliente");
            continue;
        }

        addr.sin_family = AF_INET;
        addr.sin_port = htons(SERVER_PORT);  // Porta do cliente, se for a mesma
        addr.sin_addr = clientes[i].ip;

        if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("connect para fechar cliente");
            close(sockfd);
            continue;
        }

        close(sockfd);
    }
}

void erro(char *msg)
{
    perror(msg);
    exit(1);
}

void sigusr1_handler(int sig) {
    close(client_fd_global);
    exit(0);
}