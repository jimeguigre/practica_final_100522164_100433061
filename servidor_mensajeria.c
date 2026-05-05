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

/* Estructura para pasar argumentos al hilo de cada cliente */
typedef struct {
    int client_sock;
    struct sockaddr_in client_addr;
} ThreadArgs;

/* ── Envío y recepción completa (sin cortes parciales de TCP) ── */

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

/* ── Envía un campo de exactamente 256 bytes (relleno con \0) ── */
static int send_field(int sock, const char *str) {
    char buf[256] = {0};
    if (str != NULL) 
        strncpy(buf, str, 255);
    return (send_todo(sock, buf, 256) == 256) ? 0 : -1;
}

/* ── Llama al servicio RPC de log ── */
void llamar_rpc_log(char *usuario, char *operacion, char *fichero) {
    char *host = getenv("LOG_RPC_IP");
    if (host == NULL) host = "localhost"; // Por defecto localhost

    CLIENT *clnt = clnt_create(host, LOG_PROG, LOG_VERS, "tcp");
    if (clnt == NULL) return;

    struct log_data data;
    data.usuario   = usuario;
    data.operacion = operacion;
    data.fichero   = fichero ? fichero : "";

    int *result = log_operacion_1(&data, clnt);
    if (result == NULL) clnt_perror(clnt, "Error RPC");
    clnt_destroy(clnt);
}

/*
 * ── Envía un mensaje/ACK desde el servidor al thread de escucha del cliente ──
 * op_protocolo: "SEND_MESSAGE" | "SEND_MESSAGE_ATTACH" | "SEND_MESS_ACK" | "SEND_MESS_ATTACH_ACK"
 * Devuelve 0 en éxito, -1 en error.
 */
int enviar_a_cliente(char *ip, int puerto, char *op_protocolo,
                     char *remitente, unsigned int id, char *msg, char *file) {
    int sock;
    struct sockaddr_in addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -1;

    addr.sin_family = AF_INET;
    addr.sin_port   = htons(puerto);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    char id_str[256] = {0};
    sprintf(id_str, "%u", id);

    /* Enviamos la operación como campo de 256 bytes */
    send_field(sock, op_protocolo);

    if (strcmp(op_protocolo, "SEND_MESSAGE") == 0 ||
        strcmp(op_protocolo, "SEND_MESSAGE_ATTACH") == 0) {
        /* Protocolo 8.6: remitente, id, mensaje [, fichero] */
        send_field(sock, remitente);
        send_field(sock, id_str);
        send_field(sock, msg);
        if (strcmp(op_protocolo, "SEND_MESSAGE_ATTACH") == 0) {
            send_field(sock, file);
        }
    } else {
        /* SEND_MESS_ACK / SEND_MESS_ATTACH_ACK: solo id [, fichero] */
        send_field(sock, id_str);
        if (strcmp(op_protocolo, "SEND_MESS_ATTACH_ACK") == 0) {
            send_field(sock, file);
        }
    }

    close(sock);
    return 0;
}

/* ── Hilo que atiende una petición de un cliente ── */
void *tratar_peticion(void *args) {
    ThreadArgs *targs = (ThreadArgs *)args;
    int client_sock = targs->client_sock;

    /* Obtenemos la IP real del cliente desde accept() */
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(targs->client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    free(targs);

    char op[256] = {0};
    if (recv_todo(client_sock, op, 256) < 0) goto fin;

    /* ── REGISTER ── */
    if (strcmp(op, "REGISTER") == 0) {
        char user[256] = {0};
        recv_todo(client_sock, user, 256);

        int res = registrar_usuario(user);
        uint8_t res_byte = (uint8_t)res;
        send_todo(client_sock, &res_byte, 1);

        printf("s> %s %s %s\n", op, user, res == 0 ? "OK" : "FAIL");
        llamar_rpc_log(user, "REGISTER", "");

    /* ── UNREGISTER ── */
    } else if (strcmp(op, "UNREGISTER") == 0) {
        char user[256] = {0};
        recv_todo(client_sock, user, 256);

        int res = eliminar_usuario(user);
        uint8_t res_byte = (uint8_t)res;
        send_todo(client_sock, &res_byte, 1);

        printf("s> %s %s %s\n", op, user, res == 0 ? "OK" : "FAIL");
        llamar_rpc_log(user, "UNREGISTER", "");

    /* ── CONNECT ── */
    } else if (strcmp(op, "CONNECT") == 0) {
        char user[256]       = {0};
        char puerto_str[256] = {0};

        recv_todo(client_sock, user, 256);
        /* El puerto llega como cadena (ej: "8080"), NO como entero binario */
        recv_todo(client_sock, puerto_str, 256);
        int puerto_cliente = atoi(puerto_str);

        int res = conectar_usuario(user, client_ip, puerto_cliente);
        uint8_t res_byte = (uint8_t)res;
        send_todo(client_sock, &res_byte, 1);

        printf("s> %s %s %s\n", op, user, res == 0 ? "OK" : "FAIL");
        llamar_rpc_log(user, "CONNECT", "");

        /* Si la conexión fue exitosa, enviamos mensajes pendientes (protocolo 8.6) */
        if (res == 0) {
            usleep(150000);  /* Pequeña espera para asegurar que el cliente ya está escuchando antes de enviar mensajes */
            MensajePendiente pendientes[50];
            int num_pend = obtener_mensajes_pendientes(user, pendientes);
            for (int i = 0; i < num_pend; i++) {
                char *op_env = (strlen(pendientes[i].nombre_fichero) > 0)
                               ? "SEND_MESSAGE_ATTACH" : "SEND_MESSAGE";
                if (enviar_a_cliente(client_ip, puerto_cliente, op_env, pendientes[i].remitente, pendientes[i].id, pendientes[i].mensaje, pendientes[i].nombre_fichero) == 0) {
                    char s_ip[16]; int s_port;
                    if (esta_conectado(pendientes[i].remitente, s_ip, &s_port) == 0) {
                        char *op_ack = (strlen(pendientes[i].nombre_fichero) > 0) ? "SEND_MESS_ATTACH_ACK" : "SEND_MESS_ACK";
                        enviar_a_cliente(s_ip, s_port, op_ack, pendientes[i].remitente, pendientes[i].id, "", pendientes[i].nombre_fichero);
                    }
                } else {
                    guardar_mensaje_pendiente(user, pendientes[i]);
                }
            }
        }

    /* ── DISCONNECT ── */
    } else if (strcmp(op, "DISCONNECT") == 0) {
        char user[256] = {0};
        recv_todo(client_sock, user, 256);

        int res = desconectar_usuario(user);
        uint8_t res_byte = (uint8_t)res;
        send_todo(client_sock, &res_byte, 1);

        printf("s> %s %s %s\n", op, user, res == 0 ? "OK" : "FAIL");
        llamar_rpc_log(user, "DISCONNECT", "");

    /* ── SEND Y SENDATTACH ── */
    } else if (strcmp(op, "SEND") == 0 || strcmp(op, "SENDATTACH") == 0) {
        char src[256] = {0}, dst[256] = {0}, msg[256] = {0}, file[256] = {0};
        int is_attach = (strcmp(op, "SENDATTACH") == 0);
        recv_todo(client_sock, src, 256);
        recv_todo(client_sock, dst, 256);
        recv_todo(client_sock, msg, 256);
        if (is_attach) recv_todo(client_sock, file, 256);

        if (!existe_usuario(dst)) {
            uint8_t res_byte = 1;
            send_todo(client_sock, &res_byte, 1);
        } else {
            unsigned int id = generar_siguiente_id(src);
            uint8_t res_byte = 0;
            send_todo(client_sock, &res_byte, 1);
            char id_str[256]; sprintf(id_str, "%u", id);
            send_field(client_sock, id_str);
            llamar_rpc_log(src, op, is_attach ? file : "");

            char d_ip[16]; int d_port;
            if (esta_conectado(dst, d_ip, &d_port) == 0) {
                char *op_cl = is_attach ? "SEND_MESSAGE_ATTACH" : "SEND_MESSAGE";
                if (enviar_a_cliente(d_ip, d_port, op_cl, src, id, msg, file) == 0) {
                    char s_ip[16]; int s_port;
                    if (esta_conectado(src, s_ip, &s_port) == 0) {
                        char *op_ack = is_attach ? "SEND_MESS_ATTACH_ACK" : "SEND_MESS_ACK";
                        enviar_a_cliente(s_ip, s_port, op_ack, src, id, "", file);
                    }
                } else {
                    desconectar_usuario(dst);
                    goto store;
                }
            } else {
            store: ;
                MensajePendiente m; strncpy(m.remitente, src, 255); m.id = id;
                strncpy(m.mensaje, msg, 255); strncpy(m.nombre_fichero, file, 255);
                guardar_mensaje_pendiente(dst, m);
            }
        }
    

    /* ── USERS ── */
    } else if (strcmp(op, "USERS") == 0) {
        char user_src[256] = {0};
        recv_todo(client_sock, user_src, 256);

        char d_ip[16]; int d_p;
        if (esta_conectado(user_src, d_ip, &d_p) != 0) {
            uint8_t res_byte = 1;
            send_todo(client_sock, &res_byte, 1);
        } else {
            char **nombres = NULL;
            int num_con = 0;
            obtener_usuarios_conectados_lista(&nombres, &num_con);
            
            uint8_t res_byte = 0;
            send_todo(client_sock, &res_byte, 1);
            char num_str[256]; sprintf(num_str, "%d", num_con);
            send_field(client_sock, num_str);

            for (int i = 0; i < num_con; i++) {
                char info[256], ip[16]; int port;
                esta_conectado(nombres[i], ip, &port);
                sprintf(info, "%s:%s:%d", nombres[i], ip, port);
                send_field(client_sock, info);
                free(nombres[i]);
            }
            if (nombres) free(nombres);
            llamar_rpc_log(user_src, "USERS", "");
        }
    }

fin:
    close(client_sock);
    pthread_exit(NULL);
}

/* ── Main: inicializa y escucha conexiones ── */
int main(int argc, char *argv[]) {
    int port = 8888; /* Puerto por defecto */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) port = atoi(argv[++i]);
    }

    inicializar_sistema();

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) { perror("Error al crear socket"); return -1; }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(port);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error en bind"); return -1;
    }

    listen(server_sock, 100);
    printf("s> init server 0.0.0.0:%d\n", port);
    printf("s>\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
        
        if (client_sock >= 0) {
            ThreadArgs *a = malloc(sizeof(ThreadArgs));
            a->client_sock = client_sock; // Corregido
            a->client_addr = client_addr; // Corregido
            
            pthread_t t;
            if (pthread_create(&t, NULL, tratar_peticion, a) != 0) {
                free(a);
                close(client_sock);
            } else {
                pthread_detach(t);
            }
        }
    return 0;
    }

}