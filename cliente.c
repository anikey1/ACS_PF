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

/* 
 * Variable global para el descriptor de socket. 
 * Se usa en el handler de señales para cerrar el socket al salir.
 */
int sd = -1;

/*
 * Handler de señales (por ejemplo, Ctrl+C).
 * Si el usuario interrumpe el programa, se envía un mensaje de salida al servidor
 * y se cierra la conexión limpiamente.
 */
void signal_handler(int sig) {
    printf("\n[CLIENTE] Desconectando...\n");
    if (sd != -1) {
        const char* salir_cmd = "exit";
        send(sd, salir_cmd, strlen(salir_cmd), 0); // Avisar al servidor antes de cerrar
        close(sd);
    }
    exit(0);
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server;      // Estructura con la dirección del servidor
    struct hostent *sp;             // Estructura para resolución DNS
    int n;                          // Para almacenar el número de bytes leídos/escritos
    char *host;                     // Nombre o IP del host (servidor)
    char buf_comando[256];          // Buffer para leer comandos del usuario
    char buf_respuesta[4096];       // Buffer para la respuesta del servidor (tamaño amplio)
    
    // ---------------------- VALIDACIÓN DE ARGUMENTOS ----------------------
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <servidor> <puerto>\n", argv[0]);
        fprintf(stderr, "Ejemplos:\n");
        fprintf(stderr, "  %s localhost 8080\n", argv[0]);
        fprintf(stderr, "  %s 192.168.1.100 8080\n", argv[0]);
        exit(1);
    }
    
    host = argv[1]; // Guardar el host recibido por argumento
    
    // ---------------------- CONFIGURACIÓN DE SEÑALES ----------------------
    signal(SIGINT, signal_handler);  // Ctrl+C
    signal(SIGTERM, signal_handler); // Terminación estándar
    
    printf("=== CLIENTE SSH ===\n");
    printf("Conectando a: %s:%s\n", host, argv[2]);
    
    // ---------------------- 1. CREAR SOCKET ----------------------
    printf("1. Creando socket del cliente...\n");
    sd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // Socket TCP
    if (sd < 0) {
        perror("Error al crear socket");
        exit(1);
    }
    
    // ---------------------- 2. CONFIGURAR DIRECCIÓN DEL SERVIDOR ----------------------
    printf("2. Configurando dirección del servidor...\n");
    memset((char *) &server, 0, sizeof(struct sockaddr_in)); // Inicializar en cero
    server.sin_family = AF_INET; // IPv4
    server.sin_port = htons((u_short) atoi(argv[2])); // Puerto recibido como argumento
    
    // Resolver el nombre de host a dirección IP
    sp = gethostbyname(argv[1]);
    if (sp == NULL) {
        fprintf(stderr, "Error: No se pudo resolver hostname '%s'\n", argv[1]);
        close(sd);
        exit(1);
    }
    // Copiar la dirección IP obtenida a la estructura 'server'
    memcpy(&server.sin_addr, sp->h_addr, sp->h_length);
    
    // ---------------------- 3. CONECTAR AL SERVIDOR ----------------------
    printf("3. Conectando al servidor...\n");
    if (connect(sd, (struct sockaddr *) &server, sizeof(struct sockaddr_in)) < 0) {
        perror("Error al conectar");
        printf("Verificar que:\n");
        printf("- El servidor esté ejecutándose en %s:%s\n", host, argv[2]);
        printf("- La dirección IP y puerto sean correctos\n");
        printf("- No haya firewall bloqueando la conexión\n");
        close(sd);
        exit(1);
    }
    
    printf("¡Conexión establecida exitosamente!\n\n");
    
    // ---------------------- 4. RECIBIR MENSAJE DE BIENVENIDA ----------------------
    n = recv(sd, buf_respuesta, sizeof(buf_respuesta), 0);
    if (n > 0) {
        buf_respuesta[n] = '\0'; // Terminar la cadena para impresión segura
        printf("%s", buf_respuesta);
    }
    
    printf("=== SESIÓN SSH INICIADA ===\n");
    printf("Escriba comandos para ejecutar en el servidor remoto.\n");
    printf("Comandos especiales: 'salir' o 'exit' para desconectar\n");
    printf("Presione Ctrl+C para forzar desconexión\n\n");
    
    // Inicializar el buffer de comandos para entrar al bucle
    strcpy(buf_comando, "");
    
    // ---------------------- 5. BUCLE PRINCIPAL DE COMUNICACIÓN ----------------------
    while (strcmp(buf_comando, "salir\n") != 0 && strcmp(buf_comando, "exit\n") != 0) {
        // Mostrar prompt
        printf("ssh> ");
        fflush(stdout);
        
        // Leer comando del usuario (fgets agrega '\n' al final)
        if (fgets(buf_comando, 256, stdin) == NULL) {
            // Si ocurre EOF (Ctrl+D), salir del bucle
            printf("\nEOF detectado, desconectando...\n");
            strcpy(buf_comando, "exit\n");
            break;
        }
        
        // Si el usuario solo presiona Enter (línea vacía), volver a pedir comando
        if (strlen(buf_comando) <= 1) {
            continue;
        }
        
        // Mostrar el comando que se enviará (removiendo el '\n' para claridad)
        char comando_mostrar[256];
        strcpy(comando_mostrar, buf_comando);
        if (comando_mostrar[strlen(comando_mostrar)-1] == '\n') {
            comando_mostrar[strlen(comando_mostrar)-1] = '\0';
        }
        printf("Enviando comando: '%s'\n", comando_mostrar);
        
        // Enviar el comando al servidor
        int bytes_enviados = send(sd, buf_comando, strlen(buf_comando), 0);
        if (bytes_enviados < 0) {
            perror("Error al enviar comando");
            break;
        }
        
        // Recibir la respuesta del servidor (bloquea hasta recibir datos)
        memset(buf_respuesta, 0, sizeof(buf_respuesta));
        n = recv(sd, buf_respuesta, sizeof(buf_respuesta), 0);
        
        if (n < 0) {
            perror("Error al recibir respuesta");
            break;
        } else if (n == 0) {
            // Si el servidor cierra la conexión
            printf("El servidor cerró la conexión\n");
            break;
        }
        
        // Mostrar la respuesta recibida
        printf("--- Respuesta del servidor (%d bytes) ---\n", n);
        write(1, buf_respuesta, n); // Escribir directamente en la salida estándar
        
        // Si la respuesta no termina en newline, agregar un salto de línea
        if (n > 0 && buf_respuesta[n-1] != '\n') {
            printf("\n");
        }
        printf("--- Fin de respuesta ---\n\n");
        
        // Si el comando enviado fue 'salir' o 'exit', terminar bucle
        if (strncmp(buf_comando, "salir", 5) == 0 || strncmp(buf_comando, "exit", 4) == 0) {
            break;
        }
    }
    
    // ---------------------- 6. CERRAR CONEXIÓN Y SALIR ----------------------
    printf("[CLIENTE] Cerrando conexión...\n");
    close(sd);
    printf("¡Desconectado del servidor!\n");
    exit(0);
    return 0; // Nunca se ejecuta por el exit anterior
}
