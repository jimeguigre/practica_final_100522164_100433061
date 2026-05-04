#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "servidor_gestor.h"
#include "log_rpc.h"

typedef struct {
    int client_sock;
    struct sockaddr_in client_addr;
} ThreadArgs;

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

void llamar_rpc_log(char *usuario, char *operacion, char *fichero) {
    char *host = getenv("LOG_RPC_IP"); 
    if (host == NULL) return;

    CLIENT *clnt = clnt_create(host, LOG_PROG, LOG_VERS, "tcp");
    if (clnt == NULL) return;

    struct log_data data;
    data.usuario = usuario;
    data.operacion = operacion;
    data.fichero = fichero;

    int *result = log_operacion_1(&data, clnt);
    if (result == NULL) {
        clnt_perror(clnt, "Error RPC");
    }
    clnt_destroy(clnt);
}

int enviar_a_cliente(char *ip, int puerto, char *op_protocolo, char *remitente, unsigned int id, char *msg, char *file) {
    int sock;
    struct sockaddr_in addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -1;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(puerto);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    char id_str[11];
    sprintf(id_str, "%u", id);

    send_todo(sock, op_protocolo, 256); 
    
    if (strcmp(op_protocolo, "SEND_MESSAGE_ATTACH") == 0 || strcmp(op_protocolo, "SEND_MESSAGE") == 0) {
        send_todo(sock, remitente, 256);
        send_todo(sock, id_str, 256);
        send_todo(sock, msg, 256);
        if (strcmp(op_protocolo, "SEND_MESSAGE_ATTACH") == 0) send_todo(sock, file, 256);
    } else { 
        send_todo(sock, id_str, 256);
        if (strcmp(op_protocolo, "SEND_MESS_ATTACH_ACK") == 0) send_todo(sock, file, 256);
    }

    close(sock);
    return 0;
}

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
        int puerto_cliente;
        recv_todo(client_sock, &puerto_cliente, sizeof(int));
        puerto_cliente = ntohl(puerto_cliente);

        int res = conectar_usuario(user, client_ip, puerto_cliente);
        uint8_t res_byte = (uint8_t)res;
        send_todo(client_sock, &res_byte, 1);
        llamar_rpc_log(user, "CONNECT", "");

        if (res == 0) {
            MensajePendiente pendientes[50];
            int num_pend = obtener_mensajes_pendientes(user, pendientes);
            for(int i = 0; i < num_pend; i++) {
                if(strlen(pendientes[i].nombre_fichero) > 0) {
                    enviar_a_cliente(client_ip, puerto_cliente, "SEND_MESSAGE_ATTACH", pendientes[i].remitente, pendientes[i].id, pendientes[i].mensaje, pendientes[i].nombre_fichero);
                } else {
                    enviar_a_cliente(client_ip, puerto_cliente, "SEND_MESSAGE", pendientes[i].remitente, pendientes[i].id, pendientes[i].mensaje, "");
                }
            } 
        }

    } else if (strcmp(op, "SENDATTACH") == 0) {
        char user_src[256] = {0}, user_dst[256] = {0}, msg[256] = {0}, file[256] = {0};
        recv_todo(client_sock, user_src, 256);
        recv_todo(client_sock, user_dst, 256);
        recv_todo(client_sock, msg, 256);
        recv_todo(client_sock, file, 256);

        if (!existe_usuario(user_dst)) {
            uint8_t res_byte = 1;
            send_todo(client_sock, &res_byte, 1);
        } else {
            uint8_t res_byte = 0; 
            send_todo(client_sock, &res_byte, 1); 
            unsigned int id = generar_siguiente_id(user_src); 
            char id_str[11];
            sprintf(id_str, "%u", id);
            send_todo(client_sock, id_str, 11); 

            llamar_rpc_log(user_src, "SENDATTACH", file); 

            char dst_ip[16];
            int dst_port;
            if (esta_conectado(user_dst, dst_ip, &dst_port) == 0) { 
                int err = enviar_a_cliente(dst_ip, dst_port, "SEND_MESSAGE_ATTACH", user_src, id, msg, file);
                if (err == 0) {
                    char src_ip[16]; int src_port;
                    if(esta_conectado(user_src, src_ip, &src_port) == 0) {
                        enviar_a_cliente(src_ip, src_port, "SEND_MESS_ATTACH_ACK", user_src, id, "", file);
                    }
                } else {
                    desconectar_usuario(user_dst);
                    MensajePendiente msg_fallido;
                    strncpy(msg_fallido.remitente, user_src, 256);
                    msg_fallido.id = id;
                    strncpy(msg_fallido.mensaje, msg, 256);
                    strncpy(msg_fallido.nombre_fichero, file, 256);
                    guardar_mensaje_pendiente(user_dst, msg_fallido);
                }
            } else {
                MensajePendiente m;
                strncpy(m.remitente, user_src, 256); m.id = id;
                strncpy(m.mensaje, msg, 256); strncpy(m.nombre_fichero, file, 256);
                guardar_mensaje_pendiente(user_dst, m);
            }
        }

    } else if (strcmp(op, "SEND") == 0) {
        char user_src[256] = {0}, user_dst[256] = {0}, msg[256] = {0};
        recv_todo(client_sock, user_src, 256);
        recv_todo(client_sock, user_dst, 256);
        recv_todo(client_sock, msg, 256);

        if (!existe_usuario(user_dst)) {
            uint8_t res_byte = 1;
            send_todo(client_sock, &res_byte, 1);
        } else {
            unsigned int id = generar_siguiente_id(user_src); 
            uint8_t res_byte = 0; 
            send_todo(client_sock, &res_byte, 1); 
            char id_str[11];
            sprintf(id_str, "%u", id);
            send_todo(client_sock, id_str, 11); 

            llamar_rpc_log(user_src, "SEND", ""); 

            char dst_ip[16];
            int dst_port;
            if (esta_conectado(user_dst, dst_ip, &dst_port) == 0) {
                int err = enviar_a_cliente(dst_ip, dst_port, "SEND_MESSAGE", user_src, id, msg, "");
                if (err == 0) {
                    char src_ip[16]; int src_port;
                    if(esta_conectado(user_src, src_ip, &src_port) == 0) {
                        enviar_a_cliente(src_ip, src_port, "SEND_MESS_ACK", user_src, id, "", "");
                    }
                } else {
                    desconectar_usuario(user_dst);
                    MensajePendiente m;
                    strncpy(m.remitente, user_src, 256); m.id = id;
                    strncpy(m.mensaje, msg, 256); memset(m.nombre_fichero, 0, 256);
                    guardar_mensaje_pendiente(user_dst, m);
                }
            } else {
                MensajePendiente m;
                strncpy(m.remitente, user_src, 256); m.id = id;
                strncpy(m.mensaje, msg, 256); memset(m.nombre_fichero, 0, 256);
                guardar_mensaje_pendiente(user_dst, m);
            }
        }

    } else if (strcmp(op, "UNREGISTER") == 0) {
        char user[256] = {0};
        recv_todo(client_sock, user, 256);
        int res = eliminar_usuario(user);
        uint8_t res_byte = (uint8_t)res;
        send_todo(client_sock, &res_byte, 1);
        llamar_rpc_log(user, "UNREGISTER", "");

    } else if (strcmp(op, "DISCONNECT") == 0) {
        char user[256] = {0};
        recv_todo(client_sock, user, 256);
        int res = desconectar_usuario(user);
        uint8_t res_byte = (uint8_t)res;
        send_todo(client_sock, &res_byte, 1);
        llamar_rpc_log(user, "DISCONNECT", "");

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

int main(int argc, char *argv[]) {
    int port = 8888; // Puerto por defecto si no se especifica
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[i+1]);
        }
    }

    inicializar_sistema();
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Error al crear socket");
        return -1;
    }
    
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
    printf("s> init server 127.0.0.1:%d\n", port);

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