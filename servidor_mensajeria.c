#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "servidor_gestor.h"  // lógica de usuarios
#include "log_rpc.h"  // lógica de operaciones generada por rpc

//Estructura para pasar el socket al hilo
typedef struct {
    int client_sock;
    struct sockaddr_in client_addr;
} ThreadArgs;

// FUNCIONES AUXILIARES DE ENVÍO Y RECEPCIÓN COMPLETA
static ssize_t send_todo(int sock, const void *buf, size_t len) {
    size_t enviado = 0;
    while (enviado < len) {
        ssize_t s = send(sock, (const char *)buf + enviado, len - enviado, 0);
        if (s <= 0) return -1;
        enviado += s;
    }
    return (ssize_t)enviado;
}

static ssize_t recv_todo(int sock, void *buf, size_t len) {
    size_t recibido = 0;
    while (recibido < len) {
        ssize_t r = recv(sock, (char *)buf + recibido, len - recibido, 0);
        if (r <= 0) return -1;
        recibido += r;
    }
    return (ssize_t)recibido;
}

// Función para llamar al servicio RPC de Log
void llamar_rpc_log(char *usuario, char *operacion, char *fichero) {
    char *host = getenv("LOG_RPC_IP"); 
    if (host == NULL) return;

    CLIENT *clnt = clnt_create(host, LOG_PROG, LOG_VERS, "tcp");
    if (clnt == NULL) return;

    // llamada a la función definida en el archivo .x
    // Ej: log_operacion_1(usuario, operacion, fichero, clnt);
    
    clnt_destroy(clnt);
}

//Hilo que procesa la petición de un cliente
void *tratar_peticion(void *args) {
    ThreadArgs *targs = (ThreadArgs *)args;
    int client_sock = targs->client_sock;
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(targs->client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    free(targs);

    char op[256] = {0};
    if (recv_todo(client_sock, op, 256) < 0) goto fin; 

    if (strcmp(op, "REGISTER") == 0) {
        char user[256] = {0};
        recv_todo(client_sock, user, 256);
        
        int res = registrar_usuario(user);
        uint8_t res_byte = (uint8_t)res;
        send_todo(client_sock, &res_byte, 1); 
        
        llamar_rpc_log(user, "REGISTER", "");

    } else if (strcmp(op, "CONNECT") == 0) {
        char user[256] = {0};
        recv_todo(client_sock, user, 256);
        // El cliente envía su puerto de escucha en la conexión
        int puerto_cliente;
        recv_todo(client_sock, &puerto_cliente, sizeof(int));
        puerto_cliente = ntohl(puerto_cliente);

        int res = conectar_usuario(user, client_ip, puerto_cliente);
        uint8_t res_byte = (uint8_t)res;
        send_todo(client_sock, &res_byte, 1);

        llamar_rpc_log(user, "CONNECT", ""); 
        
        // TODO: Si res == 0, enviar mensajes pendientes aquí (Protocolo 2.3)

    } else if (strcmp(op, "SENDATTACH") == 0) {
        char user_src[256] = {0}, user_dst[256] = {0}, msg[256] = {0}, file[256] = {0};
        recv_todo(client_sock, user_src, 256);
        recv_todo(client_sock, user_dst, 256);
        recv_todo(client_sock, msg, 256);
        recv_todo(client_sock, file, 256);

        unsigned int id = generar_siguiente_id(user_src); 
        
        // 1. Responder al remitente éxito y el ID asignado
        uint8_t res_byte = 0; // Asumiendo éxito para el ejemplo
        send_todo(client_sock, &res_byte, 1); 
        char id_str[11];
        sprintf(id_str, "%u", id);
        send_todo(client_sock, id_str, 11); 

        llamar_rpc_log(user_src, "SENDATTACH", file); 

        // 2. Intentar entrega al destinatario (Protocolo servidor-cliente) 
        // 3. Notificar al remitente (ACK) (Protocolo 2.3) 

    } else if (strcmp(op, "USERS") == 0) {
        char user_src[256] = {0};
        recv_todo(client_sock, user_src, 256);

        char *buffer_users;
        int num_con;
        obtener_usuarios_conectados(&buffer_users, &num_con); 

        uint8_t res_byte = 0;
        send_todo(client_sock, &res_byte, 1); 
        
        char num_str[10];
        sprintf(num_str, "%d", num_con);
        send_todo(client_sock, num_str, 10);
        
        send_todo(client_sock, buffer_users, strlen(buffer_users) + 1); 
        free(buffer_users);
        
        llamar_rpc_log(user_src, "USERS", ""); 
    }

fin:
    close(client_sock); 
    pthread_exit(NULL);
}
    

// main del servidor: inicializa, escucha conexiones y lanza hilos para cada cliente
int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s <PUERTO>\n", argv[0]);
        return -1;
    }

    inicializar_sistema();
    int port = atoi(argv[1]);
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Error al crear socket");
        return -1;
    }
    
    //Permite reutilizar el puerto instantáneamente tras cerrar el servidor
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error en bind");
        return -1;
    }

    listen(server_sock, 100);
    printf("Servidor TCP escuchando en puerto %d...\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);

        if (client_sock >= 0) {
            ThreadArgs *args = malloc(sizeof(ThreadArgs));
            args->client_sock = client_sock;
            args->client_addr = client_addr;
            
            pthread_t thread_id;
            pthread_create(&thread_id, NULL, tratar_peticion, (void *)args);
            pthread_detach(thread_id);
        }
    }
    
    return 0;
}