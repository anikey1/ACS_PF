# Proyecto Final - Cliente Servidor SSH

Este proyecto simula una sesión de conexión tipo SSH (Secure Shell), implementando un cliente y un servidor en lenguaje C, usando **sockets TCP/IP** bajo sistemas Unix/Linux/MacOS.

## 🌐 Arquitectura

El cliente se conecta a un servidor remoto y envía comandos que se ejecutan en el servidor. La salida de estos comandos se devuelve al cliente. 

## 📂 Estructura del Proyecto

```
├── cliente.c       # Código fuente del cliente
├── servidor.c      # Código fuente del servidor
└── README.md       # Instrucciones de uso y explicación del proyecto
```

## 🔧 Requisitos

- Sistema operativo Linux o MacOS
- Compilador GCC
- Conexión en red (localhost o remota)

## 🚀 Compilación

```bash
gcc -o cliente cliente.c
gcc -o servidor servidor.c
```

## 🖥️ Ejecución
# 1. Iniciar el servidor
```bash
./servidor <puerto>
```
## Ejemplo
```bash
./servidor 8080
```

## 2. Iniciar el cliente (en otra terminal o máquina)
```bash
./cliente <IP-servidor> <puerto>
```

## Ejemplos
```bash
./cliente 127.0.0.1 8080       # Conexión local
./cliente 192.168.1.100 8080   # Conexión remota
```

## 3. Usar comandos dentro del cliente
```bash
ls -l
pwd
whoami
date
ps -e
```

## 4. Para terminar la sesión
```bash
exit
```
## O también
```bash
salir
```
## También puedes usar Ctrl+C


## ⚠️ Consideraciones

- El servidor no permite comandos de salida dinámica interactivos como `top`, `vim`, `nano`, etc.
- Usa comandos simples de una línea que no cambien de directorio.
- Cliente y servidor manejan correctamente interrupciones.

## 👨‍💻 Autores

**Anikey Andrea Gómez Guzman**  
**Marco Alejandro Vigi Garduño**  
Estudiantes de Ingeniería en Computación  
Facultad de Ingeniería - UNAM
