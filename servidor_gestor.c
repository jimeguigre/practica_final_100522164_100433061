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
    // Inicializa la lista de usuarios y el contador. Se llama al iniciar el servidor.
    pthread_mutex_lock(&mutex_usuarios); // Bloqueamos mientras inicializamos
    total_usuarios = 0; // No hay usuarios registrados al inicio
    for (int i = 0; i < MAX_USUARIOS; i++) {
        usuarios[i].conectado = 0; // Todos empiezan desconectados
        usuarios[i].num_pendientes = 0; // No hay mensajes pendientes al inicio
        usuarios[i].ultimo_id = 0; // empieza en 0
    }
    pthread_mutex_unlock(&mutex_usuarios); // Desbloqueamos después de inicializar
    return 0;
}

int registrar_usuario(char *nombre) {
    pthread_mutex_lock(&mutex_usuarios); // Bloqueamos para modificar la lista de usuarios
    
    // se verifica si ya existe un usuario con el mismo nombre
    for (int i = 0; i < total_usuarios; i++) { // Recorremos la lista de usuarios registrados
        if (strcmp(usuarios[i].nombre, nombre) == 0) { // Si encontramos un usuario con el mismo nombre
            pthread_mutex_unlock(&mutex_usuarios); // Desbloqueamos antes de retornar
            return 1; // Ya existe
        }
    }

    // se crea el nuevo usuario
    if (total_usuarios < MAX_USUARIOS) { // Verificamos que no hayamos alcanzado el límite de usuarios
        strncpy(usuarios[total_usuarios].nombre, nombre, 255); // Copiamos el nombre al nuevo usuario, asegurando no exceder el tamaño del campo
        usuarios[total_usuarios].conectado = 0; // El usuario se registra pero no está conectado aún
        usuarios[total_usuarios].ultimo_id = 0; // El ID de mensajes empieza en 0 para cada usuario
        usuarios[total_usuarios].num_pendientes = 0; // No hay mensajes pendientes al registrar
        total_usuarios++; // Incrementamos el contador de usuarios registrados
        pthread_mutex_unlock(&mutex_usuarios); // Desbloqueamos después de modificar la lista
        return 0;
    }

    pthread_mutex_unlock(&mutex_usuarios); // Desbloqueamos antes de retornar
    return 2; // Error de almacenamiento
}

int conectar_usuario(char *nombre, char *ip, int puerto) {
    pthread_mutex_lock(&mutex_usuarios); // Bloqueamos para modificar el estado del usuario
    for (int i = 0; i < total_usuarios; i++) {
        if (strcmp(usuarios[i].nombre, nombre) == 0) {
            if (usuarios[i].conectado) {
                pthread_mutex_unlock(&mutex_usuarios); // Desbloqueamos antes de retornar
                return 2; // ya conectado
            }
            usuarios[i].conectado = 1; // Marcamos al usuario como conectado
            strncpy(usuarios[i].ip, ip, 15); // Guardamos la IP del cliente, asegurando no exceder el tamaño del campo
            usuarios[i].puerto = puerto; // Guardamos el puerto del cliente
            pthread_mutex_unlock(&mutex_usuarios); // Desbloqueamos después de modificar el estado del usuario
            return 0; //  se conecta al usuario exitosamente
        }
    }
    pthread_mutex_unlock(&mutex_usuarios);
    return 1; // no existe
}

int desconectar_usuario(char *nombre) {
    pthread_mutex_lock(&mutex_usuarios); // Bloqueamos para modificar el estado del usuario
    for (int i = 0; i < total_usuarios; i++) {
        if (strcmp(usuarios[i].nombre, nombre) == 0) {
            if (!usuarios[i].conectado) {         
                pthread_mutex_unlock(&mutex_usuarios); // Desbloqueamos antes de retornar
                return 2; // existe pero no conectado
            }
            usuarios[i].conectado = 0;
            pthread_mutex_unlock(&mutex_usuarios); // Desbloqueamos después de modificar el estado del usuario
            return 0;
        }
    }
    pthread_mutex_unlock(&mutex_usuarios); // Desbloqueamos antes de retornar
    return 1; // no existe el usuario 
}

unsigned int generar_siguiente_id(char *nombre_remitente) {
    unsigned int id_a_retornar = 0;
    pthread_mutex_lock(&mutex_usuarios); // Bloqueamos para modificar el ID del usuario
    for (int i = 0; i < total_usuarios; i++) {
        if (strcmp(usuarios[i].nombre, nombre_remitente) == 0) {
            usuarios[i].ultimo_id++; // Incrementamos el último ID del usuario
            // Si llega al máximo de unsigned int, el siguiente será 1 
            if (usuarios[i].ultimo_id == 0) {
                usuarios[i].ultimo_id = 1; // Reiniciamos a 1 para evitar usar el ID 0, que podría ser reservado o causar confusión
            }
            id_a_retornar = usuarios[i].ultimo_id; // Guardamos el ID a retornar antes de desbloquear
            break;
        }
    }
    pthread_mutex_unlock(&mutex_usuarios); // Desbloqueamos después de modificar el ID del usuario
    return id_a_retornar;
}

int obtener_usuarios_conectados(char **buffer, int *num_usuarios) {
    pthread_mutex_lock(&mutex_usuarios); // Bloqueamos para leer la lista de usuarios conectados
    
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

int obtener_usuarios_conectados_lista(char ***lista_nombres, int *num_usuarios) {
    pthread_mutex_lock(&mutex_usuarios);
    int contador = 0;
    for (int i = 0; i < total_usuarios; i++) {
        if (usuarios[i].conectado) contador++; // Contamos cuántos usuarios están conectados para reservar memoria
    }

    char **nombres = malloc(contador * sizeof(char *)); // Reservamos memoria para el array de punteros a nombres
    int j = 0;
    for (int i = 0; i < total_usuarios; i++) {
        if (usuarios[i].conectado) {
            nombres[j] = strdup(usuarios[i].nombre); // Reservamos memoria para cada nombre y lo copiamos
            j++;
        }
    }
    *lista_nombres = nombres; // Devolvemos el array de nombres y la cantidad de usuarios conectados
    *num_usuarios = contador; // Devolvemos el número de usuarios conectados
    pthread_mutex_unlock(&mutex_usuarios); // Desbloqueamos después de leer la lista de usuarios conectados
    return 0;
}

int guardar_mensaje_pendiente(char *destinatario, MensajePendiente msg) {
    pthread_mutex_lock(&mutex_usuarios); // Bloqueamos para modificar la lista de mensajes pendientes del destinatario
    for (int i = 0; i < total_usuarios; i++) {
        if (strcmp(usuarios[i].nombre, destinatario) == 0) {
            if (usuarios[i].num_pendientes < 50) {
                usuarios[i].mensajes[usuarios[i].num_pendientes] = msg; // Guardamos el mensaje pendiente en la posición correspondiente
                usuarios[i].num_pendientes++; // Incrementamos el contador de mensajes pendientes del destinatario
                pthread_mutex_unlock(&mutex_usuarios); // Desbloqueamos después de modificar la lista de mensajes pendientes
                return 0;
            }
        }
    }
    pthread_mutex_unlock(&mutex_usuarios); // Desbloqueamos antes de retornar
    return -1;
}

int esta_conectado(char *nombre, char *ip, int *puerto) {
    pthread_mutex_lock(&mutex_usuarios);
    for (int i = 0; i < total_usuarios; i++) {
        if (strcmp(usuarios[i].nombre, nombre) == 0) {
            if (usuarios[i].conectado) {
                strcpy(ip, usuarios[i].ip); // Copiamos la IP del usuario al buffer proporcionado
                *puerto = usuarios[i].puerto; // Guardamos el puerto del usuario en la variable proporcionada
                pthread_mutex_unlock(&mutex_usuarios); // Desbloqueamos después de leer la información del usuario
                return 0; // Sí está conectado
            }
            break;
        }
    }
    pthread_mutex_unlock(&mutex_usuarios);
    return 1; // No está conectado o no existe
}

int eliminar_usuario(char *nombre) {
    pthread_mutex_lock(&mutex_usuarios);
    for (int i = 0; i < total_usuarios; i++) {
        if (strcmp(usuarios[i].nombre, nombre) == 0) {
            // Movemos el último usuario a la posición actual para "borrarlo" y mantenemos el array compacto
            usuarios[i] = usuarios[total_usuarios - 1]; // Sobrescribimos el usuario a eliminar con el último usuario registrado
            total_usuarios--; // Decrementamos el contador de usuarios registrados
            pthread_mutex_unlock(&mutex_usuarios); // Desbloqueamos después de modificar la lista de usuarios
            return 0; // Éxito
        }
    }
    pthread_mutex_unlock(&mutex_usuarios);
    return 1; // Usuario no existe
}

// Función auxiliar para extraer los mensajes guardados al conectar
int obtener_mensajes_pendientes(char *nombre, MensajePendiente *buffer_msg) {
    pthread_mutex_lock(&mutex_usuarios);
    int cantidad = 0;
    for (int i = 0; i < total_usuarios; i++) {
        if (strcmp(usuarios[i].nombre, nombre) == 0) {
            cantidad = usuarios[i].num_pendientes; // Guardamos la cantidad de mensajes pendientes para devolverla al cliente
            for(int j = 0; j < cantidad; j++) {
                buffer_msg[j] = usuarios[i].mensajes[j]; // Copiamos los mensajes pendientes al buffer proporcionado para enviarlos al cliente
            }
            usuarios[i].num_pendientes = 0; // Se vacían porque los vamos a enviar
            break;
        }
    }
    pthread_mutex_unlock(&mutex_usuarios);
    return cantidad;
}

int existe_usuario(char *nombre) {
    pthread_mutex_lock(&mutex_usuarios);
    for (int i = 0; i < total_usuarios; i++) {
        if (strcmp(usuarios[i].nombre, nombre) == 0) {
            pthread_mutex_unlock(&mutex_usuarios);
            return 1; // Sí existe
        }
    }
    pthread_mutex_unlock(&mutex_usuarios);
    return 0; // No existe
}