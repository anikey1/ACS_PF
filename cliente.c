/*
 * Uso: ./cliente <servidor> <puerto>
 * 
 * Compilación: gcc -o cliente cliente.c
 * 
 * Descripción:
 * Este programa implementa un cliente simple tipo SSH, que permite conectar a un servidor TCP
 * remoto, enviarle comandos y recibir la respuesta. Permite salir escribiendo 'exit' o 'salir',
 * o forzar la desconexión con Ctrl+C (SIGINT).
 */

#include <stdio.h>      // Entrada/Salida estándar (printf, fgets, etc)
#include <stdlib.h>     // Funciones generales (exit, atoi, etc)
#include <string.h>     // Manejo de cadenas (memset, strcmp, etc)
#include <sys/types.h>  // Tipos de datos para sockets
#include <sys/socket.h> // Funciones y constantes para sockets
#include <netinet/in.h> // Estructuras para direcciones de red
#include <arpa/inet.h>  // Conversión de direcciones IP
#include <netdb.h>      // Resolución de nombres de host (gethostbyname)
#include <unistd.h>     // Funciones POSIX (close, write, etc)
#include <limits.h>     // Constantes de límites (no se usa en este código)
#include <signal.h>     // Manejo de señales (signal)
#include <errno.h>      // Manejo de errores (perror)

#define CMD_EOF_MARKER "<CMD_EOF>"     // Marcador de fin de salida de comando (debe coincidir con el servidor)
#define CLIENT_BUFFER_SIZE 4096       // Tamaño del buffer para la respuesta del servidor

// Variable global para el descriptor de socket. 
// Se usa en el handler de señales para cerrar el socket al salir.
int sd = -1;

/*
 * Handler de señales (por ejemplo, Ctrl+C).
 * Si el usuario interrumpe el programa, se envía un mensaje de salida al servidor
 * y se cierra la conexión limpiamente.
 */
void signal_handler(int sig) {
    printf("\n[CLIENTE] Interrupción recibida (señal %d). Desconectando...\n", sig);
    if (sd != -1) {
        const char* salir_cmd = "exit"; // Comando para avisar al servidor
        send(sd, salir_cmd, strlen(salir_cmd), MSG_NOSIGNAL); // Avisar al servidor antes de cerrar
        close(sd);
        sd = -1; 
    }
    printf("[CLIENTE] Socket cerrado. Saliendo.\n");
    exit(0);
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr; // Estructura con la dirección del servidor
    struct hostent *sp;            // Estructura para resolución DNS
    int n_recv;                    // Para almacenar el número de bytes leídos/escritos
    char *host;                    // Nombre o IP del host (servidor)
    char buf_comando[256];         // Buffer para leer comandos del usuario
    char buf_respuesta[CLIENT_BUFFER_SIZE]; // Buffer para la respuesta del servidor
    
    // ---------------------- VALIDACIÓN DE ARGUMENTOS ----------------------
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <servidor> <puerto>\n", argv[0]);
        fprintf(stderr, "Ejemplos:\n  %s localhost 8080\n  %s 192.168.1.100 8080\n", argv[0], argv[0]);
        exit(1);
    }
    host = argv[1]; // Guardar el host recibido por argumento
    
    // ---------------------- CONFIGURACIÓN DE SEÑALES ----------------------
    signal(SIGINT, signal_handler);  // Ctrl+C
    signal(SIGTERM, signal_handler); // Terminación estándar
    signal(SIGPIPE, SIG_IGN);
    
    printf("=== CLIENTE SSH ===\n");
    printf("Conectando a: %s:%s\n", host, argv[2]);
    
    // ---------------------- 1. CREAR SOCKET ----------------------
    printf("1. Creando socket del cliente...\n");
    sd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // Socket TCP
    if (sd < 0) {
        perror("[CLIENTE] Error al crear socket");
        exit(1);
    }
    
    // ---------------------- 2. CONFIGURAR DIRECCIÓN DEL SERVIDOR ----------------------
    printf("2. Configurando dirección del servidor...\n");
    memset((char *) &server_addr, 0, sizeof(struct sockaddr_in)); // Inicializar en cero
    server_addr.sin_family = AF_INET; // IPv4
    server_addr.sin_port = htons((u_short) atoi(argv[2])); // Puerto recibido como argumento
    
    // Resolver el nombre de host a dirección IP
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0) {
        sp = gethostbyname(argv[1]); // Si falla, intenta resolver hostname
        if (sp == NULL) {
            fprintf(stderr, "Error: No se pudo resolver hostname '%s'\n", argv[1]);
            close(sd);
            exit(1);
        }
        // Copiar la dirección IP obtenida a la estructura 'server'
        memcpy(&server_addr.sin_addr, sp->h_addr_list[0], sp->h_length);
    }
    
    // ---------------------- 3. CONECTAR AL SERVIDOR ----------------------
    printf("3. Conectando al servidor...\n");
    if (connect(sd, (struct sockaddr *) &server_addr, sizeof(struct sockaddr_in)) < 0) {
        perror("Error al conectar");
        printf("Verificar que:\n");
        printf("- El servidor esté ejecutándose en %s:%s\n", host, argv[2]);
        printf("- La dirección IP y puerto sean correctos\n");
        printf("- No haya firewall bloqueando la conexión\n");
        close(sd);
        exit(1);
    }
    
    printf("¡Conexión establecida exitosamente!\n\n");
    
    // ---------------------- 4. RECIBIR MENSAJES INICIALES DEL SERVIDOR ----------------------
    memset(buf_respuesta, 0, sizeof(buf_respuesta));
    n_recv = recv(sd, buf_respuesta, sizeof(buf_respuesta) - 1, 0);
    if (n_recv > 0) {
        buf_respuesta[n_recv] = '\0';  // Terminar la cadena para impresión segura
        printf("%s", buf_respuesta); // Mostrar lo que envíe el servidor (info conexión + bienvenida)
    } else if (n_recv == 0) {
        printf("El servidor cerró la conexión inmediatamente.\n");
        close(sd); exit(1);
    } else {
        perror("Error al recibir mensaje inicial del servidor");
        close(sd); exit(1);
    }
    
    printf("=== SESIÓN SSH INICIADA ===\n");
    printf("Escriba comandos para ejecutar en el servidor remoto.\n");
    printf("Comandos especiales: 'salir' o 'exit' para desconectar\n");
    printf("Presione Ctrl+C para forzar desconexión\n\n");
    
    // ---------------------- 5. BUCLE PRINCIPAL DE COMUNICACIÓN ----------------------
    while (1) {
        printf("ssh> ");
        fflush(stdout);
        
        // Leer comando del usuario (fgets agrega '\n' al final)
        if (fgets(buf_comando, sizeof(buf_comando), stdin) == NULL) {
            // Si ocurre EOF (Ctrl+D), salir del bucle
            printf("\nEOF detectado, desconectando...\n");
            strcpy(buf_comando, "exit");
        }
        buf_comando[strcspn(buf_comando, "\n")] = '\0'; // Quitar newline de fgets

        if (strlen(buf_comando) == 0) continue; // Si solo se presiona Enter
        
        printf("Enviando comando: '%s'\n", buf_comando);
        
        // Enviar el comando al servidor
        if (send(sd, buf_comando, strlen(buf_comando), 0) < 0) {
            perror("Error al enviar comando"); break;
        }

        if (strcmp(buf_comando, "salir") == 0 || strcmp(buf_comando, "exit") == 0) {
            // Recibir la respuesta del servidor (bloquea hasta recibir datos)
            memset(buf_respuesta, 0, sizeof(buf_respuesta));
            n_recv = recv(sd, buf_respuesta, sizeof(buf_respuesta) - 1, 0);
            if (n_recv > 0) {
                buf_respuesta[n_recv] = '\0';
                printf("%s", buf_respuesta); 
            }
            printf("Desconexión solicitada.\n");
            break; 
        }
        
        // Mostrar la respuesta recibida
        printf("--- Respuesta del Servidor ---\n");
        int eof_encontrado = 0;
        while (!eof_encontrado) {
            memset(buf_respuesta, 0, sizeof(buf_respuesta));
            n_recv = recv(sd, buf_respuesta, sizeof(buf_respuesta) - 1, 0);
            
            if (n_recv < 0) { perror("Error al recibir respuesta"); eof_encontrado = 1; break; }
            if (n_recv == 0) { printf("Servidor cerró conexión inesperadamente.\n"); eof_encontrado = 1; break; }
            
            buf_respuesta[n_recv] = '\0';
            // respuesta_total_len += n_recv;

            char *eof_ptr = strstr(buf_respuesta, CMD_EOF_MARKER);
            if (eof_ptr != NULL) {
                *eof_ptr = '\0'; // Terminar la cadena en el marcador
                write(STDOUT_FILENO, buf_respuesta, strlen(buf_respuesta)); // Escribir la porción útil
                eof_encontrado = 1;
            } else {
                write(STDOUT_FILENO, buf_respuesta, n_recv); // Escribir todo el chunk
            }
            fflush(stdout);
        }

        printf("--- Fin de respuesta ---\n\n");
    }
    
    // ---------------------- 6. CERRAR CONEXIÓN Y SALIR ----------------------
    printf("[CLIENTE] Cerrando conexión...\n");
    if (sd != -1) { close(sd); sd = -1; }
    printf("¡Desconectado del servidor!\n");
    exit(0);
}