#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "servidor_gestor.h"

// definición de variables globales para la gestión de usuarios y mensajes pendientes
#define MAX_USUARIOS 100

Usuario usuarios[MAX_USUARIOS];
int total_usuarios = 0;

// Mutex para proteger el acceso a la lista de usuarios (Crítico para hilos)
pthread_mutex_t mutex_usuarios = PTHREAD_MUTEX_INITIALIZER;


int inicializar_sistema() {
    pthread_mutex_lock(&mutex_usuarios);
    total_usuarios = 0;
    for (int i = 0; i < MAX_USUARIOS; i++) {
        usuarios[i].conectado = 0;
        usuarios[i].num_pendientes = 0;
        usuarios[i].ultimo_id = 0; // empieza en 0
    }
    pthread_mutex_unlock(&mutex_usuarios);
    return 0;
}

int registrar_usuario(char *nombre) {
    pthread_mutex_lock(&mutex_usuarios);
    
    // se verifica si ya existe un usuario con el mismo nombre
    for (int i = 0; i < total_usuarios; i++) {
        if (strcmp(usuarios[i].nombre, nombre) == 0) {
            pthread_mutex_unlock(&mutex_usuarios);
            return 1; // Ya existe
        }
    }

    // se crea el nuevo usuario
    if (total_usuarios < MAX_USUARIOS) {
        strncpy(usuarios[total_usuarios].nombre, nombre, 255);
        usuarios[total_usuarios].conectado = 0;
        usuarios[total_usuarios].ultimo_id = 0;
        usuarios[total_usuarios].num_pendientes = 0;
        total_usuarios++;
        pthread_mutex_unlock(&mutex_usuarios);
        return 0;
    }

    pthread_mutex_unlock(&mutex_usuarios);
    return 2; // Error de almacenamiento
}

int conectar_usuario(char *nombre, char *ip, int puerto) {
    pthread_mutex_lock(&mutex_usuarios);
    for (int i = 0; i < total_usuarios; i++) {
        if (strcmp(usuarios[i].nombre, nombre) == 0) {
            if (usuarios[i].conectado) {
                pthread_mutex_unlock(&mutex_usuarios);
                return 2; // ya conectado
            }
            usuarios[i].conectado = 1;
            strncpy(usuarios[i].ip, ip, 15);
            usuarios[i].puerto = puerto;
            pthread_mutex_unlock(&mutex_usuarios);
            return 0; //  se conecta al usuario exitosamente
        }
    }
    pthread_mutex_unlock(&mutex_usuarios);
    return 1; // no existe
}

int desconectar_usuario(char *nombre) {
    pthread_mutex_lock(&mutex_usuarios);
    for (int i = 0; i < total_usuarios; i++) {
        if (strcmp(usuarios[i].nombre, nombre) == 0) {
            usuarios[i].conectado = 0;
            pthread_mutex_unlock(&mutex_usuarios);
            return 0; // se desconecta al usuario exitosamente
        }
    }
    pthread_mutex_unlock(&mutex_usuarios);
    return 1; // no existe el usuario 
}

unsigned int generar_siguiente_id(char *nombre_remitente) {
    unsigned int id_a_retornar = 0;
    pthread_mutex_lock(&mutex_usuarios);
    for (int i = 0; i < total_usuarios; i++) {
        if (strcmp(usuarios[i].nombre, nombre_remitente) == 0) {
            usuarios[i].ultimo_id++;
            // Si llega al máximo de unsigned int, el siguiente será 1 
            if (usuarios[i].ultimo_id == 0) {
                usuarios[i].ultimo_id = 1;
            }
            id_a_retornar = usuarios[i].ultimo_id;
            break;
        }
    }
    pthread_mutex_unlock(&mutex_usuarios);
    return id_a_retornar;
}

int obtener_usuarios_conectados(char **buffer, int *num_usuarios) {
    pthread_mutex_lock(&mutex_usuarios);
    
    char temp[2048] = ""; // Buffer temporal para concatenar
    int contador = 0;

    for (int i = 0; i < total_usuarios; i++) {
        if (usuarios[i].conectado) {
            char linea[512];
            // Formato requerido: usuario: IP: puerto 
            sprintf(linea, "%s: %s: %d\n", usuarios[i].nombre, usuarios[i].ip, usuarios[i].puerto);
            strcat(temp, linea);
            contador++;
        }
    }

    *buffer = strdup(temp); // Reserva memoria dinámica con el resultado
    *num_usuarios = contador;
    
    pthread_mutex_unlock(&mutex_usuarios);
    return 0;
}

int guardar_mensaje_pendiente(char *destinatario, MensajePendiente msg) {
    pthread_mutex_lock(&mutex_usuarios); 
    for (int i = 0; i < total_usuarios; i++) {
        if (strcmp(usuarios[i].nombre, destinatario) == 0) {
            if (usuarios[i].num_pendientes < 50) {
                usuarios[i].mensajes[usuarios[i].num_pendientes] = msg;
                usuarios[i].num_pendientes++;
                pthread_mutex_unlock(&mutex_usuarios);
                return 0;
            }
        }
    }
    pthread_mutex_unlock(&mutex_usuarios);
    return -1;
}