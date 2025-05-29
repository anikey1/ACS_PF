/*

 * Uso: ./servidor <puerto>
 * 
 * Compilación: gcc -o servidor servidor.c
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
#include <time.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>

#define QLEN 2
#define BUFFER_SIZE 4096

// Variable global para manejo de señales
int fd_s = -1;

// Función para manejar señales (Ctrl+C)
void signal_handler(int sig) {
    printf("\n[SERVIDOR] Cerrando servidor...\n");
    if (fd_s != -1) {
        close(fd_s);
        shutdown(fd_s, SHUT_RDWR);
    }
    exit(0);
}

// Función para ejecutar comando y capturar salida usando popen()
char* ejecutar_comando(const char* comando) {
    FILE *fp;
    char *resultado = NULL;
    char *temp_resultado = NULL;
    char buffer[BUFFER_SIZE];
    int total_size = 0;
    
    // Verificar comando válido
    if (comando == NULL || strlen(comando) == 0) {
        resultado = malloc(50);
        strcpy(resultado, "Error: Comando vacío\n");
        return resultado;
    }
    
    printf("[SERVIDOR] Ejecutando comando: %s\n", comando);
    
    // Usar popen() para ejecutar comando y capturar salida
    fp = popen(comando, "r");
    if (fp == NULL) {
        resultado = malloc(100);
        snprintf(resultado, 100, "Error: No se pudo ejecutar '%s'\n", comando);
        return resultado;
    }
    
    // Inicializar resultado
    resultado = malloc(1);
    resultado[0] = '\0';
    
    // Leer salida del comando línea por línea
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        int buffer_len = strlen(buffer);
        temp_resultado = realloc(resultado, total_size + buffer_len + 1);
        
        if (temp_resultado == NULL) {
            free(resultado);
            pclose(fp);
            resultado = malloc(50);
            strcpy(resultado, "Error: Memoria insuficiente\n");
            return resultado;
        }
        
        resultado = temp_resultado;
        strcpy(resultado + total_size, buffer);
        total_size += buffer_len;
    }
    
    // Cerrar pipe y verificar estado
    int status = pclose(fp);
    
    // Si no hay salida, indicarlo
    if (total_size == 0) {
        free(resultado);
        resultado = malloc(100);
        if (status == 0) {
            snprintf(resultado, 100, "[Info] Comando '%s' ejecutado (sin salida)\n", comando);
        } else {
            snprintf(resultado, 100, "[Error] Comando '%s' falló\n", comando);
        }
    }
    
    return resultado;
}

void main(int argc, char *argv[]) {
    struct sockaddr_in servidor;
    struct sockaddr_in cliente;
    struct hostent* info_cliente;
    int fd_c;
    int longClient;
    char buf_comando[256];
    char *buf_respuesta;
    
    // Verificar argumentos
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        fprintf(stderr, "Ejemplo: %s 8080\n", argv[0]);
        exit(1);
    }
    
    // Configurar manejador de señales
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
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
    
    // Configurar SO_REUSEADDR para reutilizar puerto
    if (setsockopt(fd_s, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1) {
        perror("Error en setsockopt");
        exit(1);
    } else {
        printf("   setsockopt configurado correctamente\n");
    }
    
    // 2. Inicializar estructura del servidor
    printf("2. Configurando dirección del servidor...\n");
    memset((char *) &servidor, 0, sizeof(servidor));
    servidor.sin_family = AF_INET;
    servidor.sin_addr.s_addr = INADDR_ANY;
    servidor.sin_port = htons((u_short) atoi(argv[1]));
    memset(&(servidor.sin_zero), '\0', 8);
    
    // 3. Bind
    printf("3. Haciendo bind al puerto...\n");
    if (bind(fd_s, (struct sockaddr *) &servidor, sizeof(servidor)) < 0) {
        perror("Error en bind");
        close(fd_s);
        exit(1);
    }
    
    // 4. Listen
    printf("4. Escuchando conexiones entrantes...\n\n");
    if (listen(fd_s, QLEN) < 0) {
        perror("Error en listen");
        close(fd_s);
        exit(1);
    }
    
    longClient = sizeof(cliente);
    
    // Bucle principal para aceptar múltiples clientes
    while(1) {
        printf("Esperando cliente...\n");
        
        // 5. Accept - esperar conexión de cliente
        fd_c = accept(fd_s, (struct sockaddr *) &cliente, &longClient);
        if (fd_c < 0) {
            perror("Error en accept");
            continue;
        }
        
        // 6. Obtener información del cliente
        info_cliente = gethostbyaddr((char *) &cliente.sin_addr, sizeof(struct in_addr), AF_INET);
        
        // Mostrar información de conexión con timestamp
        time_t T = time(NULL);
        struct tm tm = *localtime(&T);
        printf("%02d/%02d/%04d %02d:%02d:%02d - Cliente conectado", 
               tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, 
               tm.tm_hour, tm.tm_min, tm.tm_sec);
        
        if (info_cliente == NULL) {
            printf(" desde IP: %s\n", inet_ntoa(cliente.sin_addr));
        } else {
            printf(" desde: %s (%s)\n", info_cliente->h_name, inet_ntoa(cliente.sin_addr));
        }
        
        // 7. Enviar mensaje de bienvenida
        const char* bienvenida = "Conexion SSH establecida. Escriba comandos o 'salir'/'exit' para desconectar.\n";
        send(fd_c, bienvenida, strlen(bienvenida), 0);
        
        // 8. Bucle de comunicación con el cliente
        do {
            // Limpiar buffer de comando
            memset(&(buf_comando), '\0', 256);
            
            // Recibir comando del cliente
            int n = recv(fd_c, buf_comando, sizeof(buf_comando), 0);
            
            if (n <= 0) {
                printf("Cliente desconectado inesperadamente\n");
                break;
            }
            
            // Null-terminate y limpiar saltos de línea
            buf_comando[n] = '\0';
            if (buf_comando[n-1] == '\n') buf_comando[n-1] = '\0';
            if (buf_comando[strlen(buf_comando)-1] == '\r') buf_comando[strlen(buf_comando)-1] = '\0';
            
            printf("Comando recibido: '%s'\n", buf_comando);
            
            // Verificar comandos de salida
            if (strcmp(buf_comando, "salir") == 0 || strcmp(buf_comando, "exit") == 0) {
                const char* despedida = "Desconectando. Hasta luego!\n";
                send(fd_c, despedida, strlen(despedida), 0);
                printf("Cliente solicitó desconexión\n");
                break;
            }
            
            // Verificar comando vacío
            if (strlen(buf_comando) == 0) {
                const char* error_vacio = "Error: Comando vacío\n";
                send(fd_c, error_vacio, strlen(error_vacio), 0);
                continue;
            }
            
            // Ejecutar comando y obtener resultado
            buf_respuesta = ejecutar_comando(buf_comando);
            
            // Enviar resultado al cliente
            if (buf_respuesta != NULL) {
                int bytes_enviados = send(fd_c, buf_respuesta, strlen(buf_respuesta), 0);
                printf("Respuesta enviada (%d bytes)\n", bytes_enviados);
                free(buf_respuesta);
            } else {
                const char* error_interno = "Error interno del servidor\n";
                send(fd_c, error_interno, strlen(error_interno), 0);
            }
            
        } while (strcmp(buf_comando, "salir") != 0 && strcmp(buf_comando, "exit") != 0);
        
        // 9. Cerrar conexión con cliente actual
        printf("Cerrando conexión con cliente...\n\n");
        close(fd_c);
    }
    
    // 10. Cerrar servidor (nunca debería llegar aquí en el bucle infinito)
    close(fd_s);
    shutdown(fd_s, SHUT_RDWR);
    exit(0);
}