#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <fcntl.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define MAX_USERS 1

// Estructura para la configuración del sistema
typedef struct {
    int limite_retiro;
    int limite_transferencia;
    int umbral_retiros;
    int umbral_transferencias;
    int num_hilos;
    char archivo_cuentas[50];
    char archivo_log[50];
} Config;

sem_t *semaforo; // Semáforo para control de acceso concurrente
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex para sincronización
int pipefd[2]; // Tubería para alertas desde monitor
int msqid; // Identificador de la cola de mensajes

// Estructura para una cuenta
struct Cuenta {
    int numero_cuenta;
    char nombre[50];
    float saldo;
    int transacciones;
};

// Registra un nuevo usuario en el archivo de cuentas
void registrar_usuario() {
    struct Cuenta nueva_cuenta;
    printf("Ingrese el nuevo número de cuenta: ");
    scanf("%d", &nueva_cuenta.numero_cuenta);
    printf("Ingrese el nombre del usuario: ");
    scanf("%49s", nueva_cuenta.nombre);
    printf("Ingrese el saldo inicial: ");
    scanf("%f", &nueva_cuenta.saldo);
    nueva_cuenta.transacciones = 0;

    sem_wait(semaforo);
    FILE *archivo = fopen("data/cuentas.dat", "ab");
    if (!archivo) {
        perror("Error al abrir el archivo de cuentas");
        sem_post(semaforo);
        exit(1);
    }
    fwrite(&nueva_cuenta, sizeof(struct Cuenta), 1, archivo);
    fclose(archivo);
    sem_post(semaforo);
    printf("Usuario registrado exitosamente.\n");
}

// Verifica si las credenciales de login son correctas
int verificar_login(int num_cuenta, char *nombre) {
    sem_wait(semaforo);
    FILE *archivo = fopen("data/cuentas.dat", "rb");
    if (!archivo) {
        perror("Error al abrir el archivo de cuentas");
        sem_post(semaforo);
        return 0;
    }
    struct Cuenta cuenta;
    while (fread(&cuenta, sizeof(struct Cuenta), 1, archivo)) {
        if (cuenta.numero_cuenta == num_cuenta && strcmp(cuenta.nombre, nombre) == 0) {
            fclose(archivo);
            sem_post(semaforo);
            return 1;
        }
    }
    fclose(archivo);
    sem_post(semaforo);
    return 0;
}

// Lee la configuración desde config.txt
Config leer_configuracion(const char *ruta) {
    Config configuracion;
    FILE *file = fopen(ruta, "r");
    if (!file) {
        perror("Error al abrir config.txt");
        exit(1);
    }
    char linea[100];
    while (fgets(linea, sizeof(linea), file)) {
        if (strstr(linea, "LIMITE_RETIRO")) sscanf(linea, "LIMITE_RETIRO = %d", &configuracion.limite_retiro);
        else if (strstr(linea, "LIMITE_TRANSFERENCIA")) sscanf(linea, "LIMITE_TRANSFERENCIA = %d", &configuracion.limite_transferencia);
        else if (strstr(linea, "UMBRAL_RETIROS")) sscanf(linea, "UMBRAL_RETIROS = %d", &configuracion.umbral_retiros);
        else if (strstr(linea, "UMBRAL_TRANSFERENCIAS")) sscanf(linea, "UMBRAL_TRANSFERENCIAS = %d", &configuracion.umbral_transferencias);
        else if (strstr(linea, "NUM_HILOS")) sscanf(linea, "NUM_HILOS = %d", &configuracion.num_hilos);
        else if (strstr(linea, "ARCHIVO_CUENTAS")) sscanf(linea, "ARCHIVO_CUENTAS = %s", configuracion.archivo_cuentas);
        else if (strstr(linea, "ARCHIVO_LOG")) sscanf(linea, "ARCHIVO_LOG = %s", configuracion.archivo_log);
    }
    fclose(file);
    return configuracion;
}

// Muestra el menú principal
void mostrar_menu(void) {
    printf("****************************************\n");
    printf("*       MENÚ DE AUTENTICACIÓN          *\n");
    printf("****************************************\n");
    printf("* 1. Iniciar sesión                    *\n");
    printf("* 2. Registrarse                       *\n");
    printf("* 3. Salir                             *\n");
    printf("****************************************\n");
    printf("Elija una opción: ");
}

int main() {
    // Leer configuración
    Config configuracion = leer_configuracion("config.txt");

    // Crear tubería para alertas
    if (pipe(pipefd) == -1) {
        perror("Error al crear tubería");
        exit(1);
    }

    // Crear cola de mensajes
    key_t key = ftok("banco.c", 65);
    msqid = msgget(key, 0666 | IPC_CREAT);
    if (msqid == -1) {
        perror("Error al crear cola de mensajes");
        exit(1);
    }

    // Crear semáforo
    semaforo = sem_open("/cuentas_sem", O_CREAT, 0644, 1);
    if (semaforo == SEM_FAILED) {
        perror("Error al crear semáforo");
        exit(1);
    }

    // Lanzar monitor.c en una nueva terminal
    pid_t pid_monitor = fork();
    if (pid_monitor == 0) {
        // Proceso hijo (monitor)
        close(pipefd[0]); // Cerrar extremo de lectura
        char msqid_str[10];
        char pipe_write_str[10];
        char limite_retiro_str[10];
        char limite_transferencia_str[10];
        char umbral_retiros_str[10];
        char umbral_transferencias_str[10];

        sprintf(msqid_str, "%d", msqid);
        sprintf(pipe_write_str, "%d", pipefd[1]);
        sprintf(limite_retiro_str, "%d", configuracion.limite_retiro);
        sprintf(limite_transferencia_str, "%d", configuracion.limite_transferencia);
        sprintf(umbral_retiros_str, "%d", configuracion.umbral_retiros);
        sprintf(umbral_transferencias_str, "%d", configuracion.umbral_transferencias);

        execlp("gnome-terminal", "gnome-terminal", "--", "./monitor",
               msqid_str, pipe_write_str, limite_retiro_str, limite_transferencia_str,
               umbral_retiros_str, umbral_transferencias_str, NULL);
        perror("Error al ejecutar gnome-terminal para monitor");
        exit(1);
    } else if (pid_monitor < 0) {
        perror("Error en fork para monitor");
        exit(1);
    }

    // Configurar lectura no bloqueante para alertas
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    // Bucle principal
    while (1) {
        // Leer alertas de la tubería
        char alerta[256];
        ssize_t bytes = read(pipefd[0], alerta, sizeof(alerta) - 1);
        if (bytes > 0) {
            alerta[bytes] = '\0';
            printf("ALERTA RECIBIDA: %s", alerta);
            fflush(stdout);
        }

        mostrar_menu();
        int opcion;
        scanf("%d", &opcion);
        while (getchar() != '\n');

        switch (opcion) {
            case 1: {
                int num_cuenta_autenticado;
                char nombre[50];
                printf("Ingrese su número de cuenta: ");
                scanf("%d", &num_cuenta_autenticado);
                printf("Ingrese su nombre: ");
                scanf("%49s", nombre);

                if (verificar_login(num_cuenta_autenticado, nombre)) {
                    printf("Inicio de sesión exitoso.\n");
                    pid_t pid = fork();
                    if (pid == 0) {
                        char num_cuenta_str[10];
                        char msqid_str[10];
                        sprintf(num_cuenta_str, "%d", num_cuenta_autenticado);
                        sprintf(msqid_str, "%d", msqid);
                        execlp("gnome-terminal", "gnome-terminal", "--", "./usuario", num_cuenta_str, msqid_str, NULL);
                        perror("Error al ejecutar gnome-terminal");
                        exit(1);
                    } else if (pid < 0) {
                        perror("Error en fork");
                        exit(1);
                    }
                } else {
                    printf("Número de cuenta o nombre incorrectos.\n");
                }
                break;
            }
            case 2:
                registrar_usuario();
                break;
            case 3:
                printf("Saliendo...\n");
                sem_unlink("/cuentas_sem");
                sem_close(semaforo);
                close(pipefd[0]);
                close(pipefd[1]);
                msgctl(msqid, IPC_RMID, NULL);
                exit(0);
            default:
                printf("Opción no válida.\n");
                break;
        }
    }

    return 0;
}
