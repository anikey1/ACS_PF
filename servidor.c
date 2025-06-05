/*
 * Uso: ./servidor <puerto>
 * Compilación: gcc -o servidor servidor.c
 * Descripción:
 * Este programa implementa un servidor TCP simple tipo SSH.
 * - Muestra logs detallados en su propia consola durante el inicio y por cada cliente/comando.
 * - Envía información detallada de la conexión al cliente, seguida de un mensaje de bienvenida.
 * - Ejecuta comandos recibidos y devuelve la salida al cliente
 */

#include <stdio.h>      // printf, fprintf, perror, fgets, snprintf
#include <stdlib.h>     // exit, malloc, free, atoi
#include <string.h>     // strlen, strcpy, strcmp, memset, strdup, strtok, strcspn
#include <sys/types.h>  // tipos de datos del sistema (pid_t, ssize_t)
#include <sys/socket.h> // socket, bind, listen, accept, send, recv, setsockopt, shutdown
#include <netinet/in.h> // estructuras sockaddr_in
#include <arpa/inet.h>  // inet_ntoa, htons
#include <netdb.h>      // gethostbyaddr
#include <unistd.h>     // close, fork, pipe, dup2, execvp, read, write, wait
#include <time.h>       // time, localtime
#include <fcntl.h>      // control de archivos
#include <limits.h>     // límites del sistema
#include <errno.h>      // códigos de error
#include <signal.h>     // manejo de señales
#include <ctype.h>      // isspace (para la función trim)
#include <sys/wait.h>   // wait (para esperar al proceso hijo)
//Librerías necesarias para sockets, manejo de strings, tiempo y señales.

#define QLEN 2                  // Cola de conexiones pendientes (para listen)
#define BUFFER_SIZE 4096        // Tamaño del buffer para E/S de comandos y pipe
#define MAX_TOKENS 64           // Máximo de argumentos para un comando
#define CMD_EOF_MARKER "<CMD_EOF>" // Marcador para el fin de la salida de un comando (enviado al cliente)

// Variable global para manejo de señales
int fd_s = -1;

// Función para manejar señales (Ctrl+C)
void signal_handler(int sig) {
    printf("\n[SERVIDOR] Cerrando servidor...\n");
    if (fd_s != -1) {
        close(fd_s);              // Cerrar socket
        shutdown(fd_s, SHUT_RDWR); // Cerrar ambas direcciones de comunicación
    }
    exit(0);
}

/*
 * Elimina los espacios en blanco al inicio y al final de la cadena.
 */
void trim(char *str) {
    char *start = str;
    char *end;
    while (isspace((unsigned char)*start)) start++;
    if (*start == '\0') {
        str[0] = '\0';
        return;
    }
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
}

/*
 * Divide una cadena en tokens (argumentos para execvp).
 */
int split(const char *buf_comando, char *arg_list[]) {
    int count = 0;
    char *token;
    char *buffer_copy = strdup(buf_comando);
    if (buffer_copy == NULL) {
        perror("[SERVIDOR] Error en strdup para split");
        return 0;
    }
    token = strtok(buffer_copy, " ");
    while (token != NULL && count < MAX_TOKENS - 1) {
        arg_list[count] = strdup(token);
        if (arg_list[count] == NULL) {
            perror("[SERVIDOR] Error en strdup para token en split");
            for (int i = 0; i < count; i++) free(arg_list[i]);
            free(buffer_copy);
            return -1;
        }
        count++;
        token = strtok(NULL, " ");
    }
    arg_list[count] = NULL;
    free(buffer_copy);
    return count;
}

/*
 * Ejecuta un comando en un proceso hijo y redirige su salida al cliente.
 * Devuelve el número total de bytes de datos del comando enviados al cliente (payload).
 */
ssize_t ejecutar_y_transmitir_comando(char *comando_base, char *arg_list[], int fd_cliente) {
    int pipe_fd[2];
    pid_t pid;
    char buffer_pipe[BUFFER_SIZE];
    ssize_t bytes_leidos_pipe;
    ssize_t total_bytes_enviados_payload = 0;

    if (pipe(pipe_fd) == -1) {
        perror("[SERVIDOR] Error al crear el pipe");
        const char* err_msg = "Error interno del servidor (pipe)\n";
        send(fd_cliente, err_msg, strlen(err_msg), 0);
        send(fd_cliente, CMD_EOF_MARKER, strlen(CMD_EOF_MARKER), 0);
        return 0;
    }

    pid = fork();

    if (pid == -1) {
        perror("[SERVIDOR] Error al crear proceso hijo (fork)");
        close(pipe_fd[0]); close(pipe_fd[1]);
        const char* err_msg = "Error interno del servidor (fork)\n";
        send(fd_cliente, err_msg, strlen(err_msg), 0);
        send(fd_cliente, CMD_EOF_MARKER, strlen(CMD_EOF_MARKER), 0);
        return 0;
    }

    if (pid == 0) { // Proceso Hijo
        close(pipe_fd[0]);
        if (dup2(pipe_fd[1], STDOUT_FILENO) == -1) { perror("[SERVIDOR HIJO] Error dup2 stdout"); exit(EXIT_FAILURE); }
        if (dup2(pipe_fd[1], STDERR_FILENO) == -1) { perror("[SERVIDOR HIJO] Error dup2 stderr"); exit(EXIT_FAILURE); }
        close(pipe_fd[1]);
        execvp(comando_base, arg_list);
        fprintf(stderr, "Error al ejecutar comando '%s': %s\n", comando_base, strerror(errno));
        exit(EXIT_FAILURE);
    } else { // Proceso Padre
        close(pipe_fd[1]);
        while ((bytes_leidos_pipe = read(pipe_fd[0], buffer_pipe, sizeof(buffer_pipe) -1 )) > 0) {
            ssize_t bytes_enviados_chunk = send(fd_cliente, buffer_pipe, bytes_leidos_pipe, 0);
            if (bytes_enviados_chunk < 0) { perror("[SERVIDOR] Error al enviar datos del pipe"); break; }
            if (bytes_enviados_chunk > 0) total_bytes_enviados_payload += bytes_enviados_chunk;
        }
        if (bytes_leidos_pipe < 0) perror("[SERVIDOR] Error al leer del pipe");
        if (send(fd_cliente, CMD_EOF_MARKER, strlen(CMD_EOF_MARKER), 0) < 0) {
            perror("[SERVIDOR] Error al enviar CMD_EOF_MARKER");
        }
        close(pipe_fd[0]);
        waitpid(pid, NULL, 0); // Esperar al hijo, sin loguear su estado en consola del servidor
    }
    return total_bytes_enviados_payload;
}
//Manejo final del resultado según si hubo salida o errores.

//Función principal del servidor
int main(int argc, char *argv[]) {
    struct sockaddr_in servidor_addr;  // Estructura para dirección del servidor
    struct sockaddr_in cliente_addr;   // Estructura para dirección del cliente
    struct hostent* info_cliente;      // Información del hostname del cliente
    int fd_c;                          // File descriptor del cliente
    socklen_t longClient;              // Longitud de la estructura cliente
    char buf_comando_raw[BUFFER_SIZE]; // Buffer para recibir comandos
    char buf_comando_trimmed[BUFFER_SIZE];
    char *arg_list[MAX_TOKENS];
    int num_tokens;
    char buffer_info_conexion_cliente[512]; // Buffer para formatear el mensaje de conexión para el cliente

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\nEjemplo: %s 8080\n", argv[0], argv[0]);
        exit(1);
    }
    
    // Configurar manejador de señales
    signal(SIGINT, signal_handler);   // Ctrl+C
    signal(SIGTERM, signal_handler);  // kill
    
    printf("=== SERVIDOR SSH INICIADO ===\n");
    printf("Puerto: %s\n", argv[1]);
    printf("Esperando conexiones...\n\n");
    
    // 1. Crear socket del servidor
    printf("1. Creando socket del servidor...\n");
    fd_s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd_s < 0) {
        perror("Error al crear socket");
        exit(1);
    }
    int optval = 1;
    //socket() crea un nuevo socket TCP/IP.
    // Configurar SO_REUSEADDR para reutilizar puerto
    if (setsockopt(fd_s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        perror("Error en setsockopt(SO_REUSEADDR)");
    } else {
        printf("   setsockopt configurado correctamente\n");
    }
    //SO_REUSEADDR permite reutilizar el puerto inmediatamente después de cerrar el servidor.
    // 2. Inicializar estructura del servidor
    printf("2. Configurando dirección del servidor...\n");
    memset((char *) &servidor_addr, 0, sizeof(servidor_addr));
    servidor_addr.sin_family = AF_INET;             // IPv4
    servidor_addr.sin_addr.s_addr = INADDR_ANY;     // Cualquier interfaz
    servidor_addr.sin_port = htons((u_short) atoi(argv[1])); // Puerto convertido a network byte order
    
    // 3. Bind, bind() asocia el socket con la dirección y puerto especificados.
    printf("3. Haciendo bind al puerto...\n");
    if (bind(fd_s, (struct sockaddr *) &servidor_addr, sizeof(servidor_addr)) < 0) {
        perror("Error en bind");
        close(fd_s);
        exit(1);
    }
    // 4. Listen, pone el socket en modo de escucha con cola de hasta QLEN (2) conexiones pendientes.
    printf("4. Escuchando conexiones entrantes...\n\n");
    if (listen(fd_s, QLEN) < 0) {
        perror("Error en listen");
        close(fd_s);
        exit(1);
    }
    
    longClient = sizeof(cliente_addr);
    
    while(1) {
        printf("Esperando cliente...\n");
        // 5. Accept - esperar conexión de cliente, bloquea hasta que llega una conexión, retorna nuevo socket para comunicarse con el cliente.
        fd_c = accept(fd_s, (struct sockaddr *) &cliente_addr, &longClient);
        if (fd_c < 0) {
            if (errno == EINTR) { printf("Accept() interrumpido. Saliendo...\n"); break; }
            perror("Error en accept"); continue; 
        }
        // 6. Obtener información del cliente
        info_cliente = gethostbyaddr((char *) &cliente_addr.sin_addr, sizeof(struct in_addr), AF_INET);
        // Mostrar información de conexión con timestamp
        time_t T = time(NULL);
        struct tm tm_info = *localtime(&T);
        
        // Imprimir en la consola del servidor
        printf("%02d/%02d/%04d %02d:%02d:%02d - Cliente conectado desde: ", 
               tm_info.tm_mday, tm_info.tm_mon + 1, tm_info.tm_year + 1900, 
               tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
        if (info_cliente == NULL) {
            printf("%s\n", inet_ntoa(cliente_addr.sin_addr));
        } else {
            printf("%s (%s)\n", info_cliente->h_name, inet_ntoa(cliente_addr.sin_addr));
        }

        // 7. Enviar mensaje de bienvenida
        memset(buffer_info_conexion_cliente, 0, sizeof(buffer_info_conexion_cliente));
        int len_escrita = snprintf(buffer_info_conexion_cliente, sizeof(buffer_info_conexion_cliente),
                 "%02d/%02d/%04d %02d:%02d:%02d - Cliente conectado desde: ",
                 tm_info.tm_mday, tm_info.tm_mon + 1, tm_info.tm_year + 1900,
                 tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
        if (info_cliente == NULL) {
            snprintf(buffer_info_conexion_cliente + len_escrita, sizeof(buffer_info_conexion_cliente) - len_escrita,
                     "%s\n", inet_ntoa(cliente_addr.sin_addr));
        } else {
            snprintf(buffer_info_conexion_cliente + len_escrita, sizeof(buffer_info_conexion_cliente) - len_escrita,
                     "%s (%s)\n", info_cliente->h_name, inet_ntoa(cliente_addr.sin_addr));
        }
        // Enviar esta información formateada al cliente
        if (send(fd_c, buffer_info_conexion_cliente, strlen(buffer_info_conexion_cliente), 0) < 0) {
            perror("Error al enviar info de conexión al cliente");
        }
        // ---- FIN DE NUEVO ----
        
        const char* bienvenida_msg = "Conexión SSH simulada. Escriba comandos o 'salir'/'exit' para desconectar.\n";
        if (send(fd_c, bienvenida_msg, strlen(bienvenida_msg), 0) < 0) {
            perror("Error al enviar mensaje de bienvenida"); close(fd_c); continue;
        }
        // 8. Bucle de comunicación con el cliente
        do {
            // Limpiar buffer de comando
            memset(buf_comando_raw, '\0', sizeof(buf_comando_raw));
            memset(buf_comando_trimmed, '\0', sizeof(buf_comando_trimmed));
            
            // Recibir comando del cliente
            int bytes_recibidos = recv(fd_c, buf_comando_raw, sizeof(buf_comando_raw) - 1, 0);
            
            if (bytes_recibidos <= 0) {
                if (bytes_recibidos != 0) perror("Cliente desconectado inesperadamente");
                break; 
            }
            
            // Null-terminate y limpiar saltos de línea
            buf_comando_raw[bytes_recibidos] = '\0';
            strncpy(buf_comando_trimmed, buf_comando_raw, sizeof(buf_comando_trimmed) -1);
            buf_comando_trimmed[strcspn(buf_comando_trimmed, "\r\n")] = 0;
            trim(buf_comando_trimmed);
            
            printf("Comando recibido: '%s'\n", buf_comando_trimmed);
            
            // Verificar comandos de salida
            if (strcmp(buf_comando_trimmed, "salir") == 0 || strcmp(buf_comando_trimmed, "exit") == 0) {
                const char* despedida_msg = "Desconectando. ¡Hasta luego!\n";
                send(fd_c, despedida_msg, strlen(despedida_msg), 0);
                break;
            }

            // Verificar comando vacío
            if (strlen(buf_comando_trimmed) == 0) {
                const char* error_vacio_msg = "Error: Comando vacío recibido.\n";
                send(fd_c, error_vacio_msg, strlen(error_vacio_msg), 0);
                send(fd_c, CMD_EOF_MARKER, strlen(CMD_EOF_MARKER), 0);
                printf("Ejecutando comando: \n");
                printf("Respuesta enviada (%zu bytes)\n", strlen(error_vacio_msg));
                continue;
            }
            
            // Ejecutar comando y obtener resultado
            printf("[SERVIDOR ]Ejecutando comando: %s\n", buf_comando_trimmed);

            num_tokens = split(buf_comando_trimmed, arg_list);
            ssize_t bytes_respuesta_payload = 0;

            if (num_tokens > 0) {
                bytes_respuesta_payload = ejecutar_y_transmitir_comando(arg_list[0], arg_list, fd_c);
                for (int i = 0; i < num_tokens; i++) { free(arg_list[i]); arg_list[i] = NULL; }
            } else {
                 const char* err_msg_proc = "Error interno del servidor.\n";
                 send(fd_c, err_msg_proc, strlen(err_msg_proc), 0);
                 send(fd_c, CMD_EOF_MARKER, strlen(CMD_EOF_MARKER), 0);
                 bytes_respuesta_payload = strlen(err_msg_proc);
            }
            printf("Respuesta enviada (%zd bytes)\n", bytes_respuesta_payload);
        } while (1); 
        // 9. Cerrar conexión con cliente actual
        printf("Cerrando conexión con cliente...\n\n");
        close(fd_c);
    }
    // 10. Cerrar servidor (nunca debería llegar aquí en el bucle infinito)
    close(fd_s);
    exit(0);
    return 0;
}