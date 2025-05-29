/*


 * Uso: ./cliente <servidor> <puerto>
 * 
 * Compilación: gcc -o cliente cliente.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <errno.h>

// Variable global para manejo de señales
int sd = -1;

// Función para manejar señales (Ctrl+C)
void signal_handler(int sig) {
    printf("\n[CLIENTE] Desconectando...\n");
    if (sd != -1) {
        // Enviar comando de salida antes de cerrar
        const char* salir_cmd = "exit";
        send(sd, salir_cmd, strlen(salir_cmd), 0);
        close(sd);
    }
    exit(0);
}

void main(int argc, char *argv[]) {
    struct sockaddr_in server;
    struct hostent *sp;
    int n;
    char *host;
    char buf_comando[256];
    char buf_respuesta[4096];  // Buffer más grande para respuestas de comandos
    
    // Verificar argumentos de línea de comandos
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <servidor> <puerto>\n", argv[0]);
        fprintf(stderr, "Ejemplos:\n");
        fprintf(stderr, "  %s localhost 8080\n", argv[0]);
        fprintf(stderr, "  %s 192.168.1.100 8080\n", argv[0]);
        exit(1);
    }
    
    host = argv[1];
    
    // Configurar manejador de señales
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("=== CLIENTE SSH ===\n");
    printf("Conectando a: %s:%s\n", host, argv[2]);
    
    // 1. Crear socket del cliente
    printf("1. Creando socket del cliente...\n");
    sd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sd < 0) {
        perror("Error al crear socket");
        exit(1);
    }
    
    // 2. Configurar estructura del servidor
    printf("2. Configurando dirección del servidor...\n");
    memset((char *) &server, 0, sizeof(struct sockaddr_in));
    server.sin_family = AF_INET;
    server.sin_port = htons((u_short) atoi(argv[2]));
    
    // Resolver hostname del servidor
    sp = gethostbyname(argv[1]);
    if (sp == NULL) {
        fprintf(stderr, "Error: No se pudo resolver hostname '%s'\n", argv[1]);
        close(sd);
        exit(1);
    }
    memcpy(&server.sin_addr, sp->h_addr, sp->h_length);
    
    // 3. Conectar al servidor
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
    
    // 4. Recibir mensaje de bienvenida del servidor
    n = recv(sd, buf_respuesta, sizeof(buf_respuesta), 0);
    if (n > 0) {
        buf_respuesta[n] = '\0';
        printf("%s", buf_respuesta);
    }
    
    printf("=== SESIÓN SSH INICIADA ===\n");
    printf("Escriba comandos para ejecutar en el servidor remoto.\n");
    printf("Comandos especiales: 'salir' o 'exit' para desconectar\n");
    printf("Presione Ctrl+C para forzar desconexión\n\n");
    
    // Inicializar buffer de comando para entrar al bucle
    strcpy(buf_comando, "");
    
    // 5. Bucle principal de comunicación
    while (strcmp(buf_comando, "salir\n") != 0 && strcmp(buf_comando, "exit\n") != 0) {
        // Mostrar prompt
        printf("ssh> ");
        fflush(stdout);
        
        // Leer comando del usuario
        if (fgets(buf_comando, 256, stdin) == NULL) {
            printf("\nEOF detectado, desconectando...\n");
            strcpy(buf_comando, "exit\n");
            break;
        }
        
        // Verificar si el comando está vacío (solo Enter)
        if (strlen(buf_comando) <= 1) {
            continue;
        }
        
        // Mostrar comando que se va a enviar (sin el \n final para claridad)
        char comando_mostrar[256];
        strcpy(comando_mostrar, buf_comando);
        if (comando_mostrar[strlen(comando_mostrar)-1] == '\n') {
            comando_mostrar[strlen(comando_mostrar)-1] = '\0';
        }
        printf("Enviando comando: '%s'\n", comando_mostrar);
        
        // Enviar comando al servidor
        int bytes_enviados = send(sd, buf_comando, strlen(buf_comando), 0);
        if (bytes_enviados < 0) {
            perror("Error al enviar comando");
            break;
        }
        
        // Recibir respuesta del servidor
        memset(buf_respuesta, 0, sizeof(buf_respuesta));
        n = recv(sd, buf_respuesta, sizeof(buf_respuesta), 0);
        
        if (n < 0) {
            perror("Error al recibir respuesta");
            break;
        } else if (n == 0) {
            printf("El servidor cerró la conexión\n");
            break;
        }
        
        // Mostrar respuesta del servidor
        printf("--- Respuesta del servidor (%d bytes) ---\n", n);
        write(1, buf_respuesta, n);
        
        // Agregar línea en blanco si la respuesta no termina en newline
        if (n > 0 && buf_respuesta[n-1] != '\n') {
            printf("\n");
        }
        printf("--- Fin de respuesta ---\n\n");
        
        // Verificar si acabamos de enviar salir/exit
        if (strncmp(buf_comando, "salir", 5) == 0 || strncmp(buf_comando, "exit", 4) == 0) {
            break;
        }
    }
    
    // 6. Cerrar conexión
    printf("[CLIENTE] Cerrando conexión...\n");
    close(sd);
    printf("¡Desconectado del servidor!\n");
    exit(0);
}