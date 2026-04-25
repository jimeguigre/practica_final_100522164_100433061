#ifndef _SERVIDOR_GESTOR_H_
#define _SERVIDOR_GESTOR_H_

#include <stdint.h>

/**
 * @brief Estructura para almacenar mensajes que no pudieron ser entregados
 * porque el destinatario estaba desconectado
 */
typedef struct {
    char remitente[256];      // Nombre del usuario que envió el mensaje 
    unsigned int id;          // Identificador único asignado por el servidor
    char mensaje[256];        // Contenido del mensaje (máx 255 + \0)
    char nombre_fichero[256]; // Nombre del fichero adjunto
} MensajePendiente;

/**
 * @brief Estructura que define a un usuario en el sistema
 */
typedef struct {
    char nombre[256];           // Identificador único del usuario 
    char ip[16];                // IP del cliente para transferencias P2P
    int puerto;                 // Puerto del thread de escucha del cliente
    int conectado;              // 1 si está conectado, 0 si no
    unsigned int ultimo_id;     // Último ID de mensaje usado por este usuario

    // Gestión de mensajes pendientes
    MensajePendiente mensajes[50]; // Buffer circular o lista de mensajes
    int num_pendientes;            // Contador de mensajes esperando entrega
} Usuario;



// Funciones del gestor de usuarios y mensajes

/**
 * @brief Inicializa las estructuras de datos de la aplicación.
 * @return 0 en caso de éxito, -1 en error.
 */
int inicializar_sistema();

/**
 * @brief Registra un nuevo usuario en el sistema[cite: 29].
 * @return 0 éxito, 1 usuario ya existe, 2 otro error[cite: 68].
 */
int registrar_usuario(char *nombre);

/**
 * @brief Elimina un usuario del sistema[cite: 29].
 */
int eliminar_usuario(char *nombre);

/**
 * @brief Conecta a un usuario y actualiza su IP y puerto[cite: 132].
 * @return 0 éxito, 1 usuario no existe, 2 ya conectado/error.
 */
int conectar_usuario(char *nombre, char *ip, int puerto);

/**
 * @brief Desconecta a un usuario del sistema[cite: 29].
 */
int desconectar_usuario(char *nombre);

/**
 * @brief Obtiene la lista de usuarios conectados con su IP y puerto[cite: 132, 147].
 * @param buffer Cadena donde se guardará el formato "usuario:IP:puerto"[cite: 147].
 * @param num_usuarios Puntero para devolver la cantidad de conectados[cite: 138].
 */
int obtener_usuarios_conectados(char **buffer, int *num_usuarios);

/**
 * @brief Incrementa y devuelve el siguiente ID de mensaje para un usuario.
 */
unsigned int generar_siguiente_id(char *nombre_remitente);

/**
 * @brief Guarda un mensaje en la cola de pendientes del destinatario[cite: 79].
 */
int guardar_mensaje_pendiente(char *destinatario, MensajePendiente msg);

#endif