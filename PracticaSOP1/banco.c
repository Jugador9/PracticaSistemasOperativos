#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <fcntl.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <signal.h> // Para usar kill()

#define MAX_USERS 1
#define MAX_PROCESOS 10 // Límite de procesos hijos a rastrear

typedef struct {
    int limite_retiro;
    int limite_transferencia;
    int umbral_retiros;
    int umbral_transferencias;
    int num_hilos;
    char archivo_cuentas[50];
    char archivo_log[50];
} Config;

// Estructura para una cuenta
struct Cuenta {
    int numero_cuenta;
    char nombre[50]; // Tamaño máximo para el nombre
    float saldo;
    int transacciones; // Contador de transacciones
};

Config config;
sem_t *semaforo;
FILE *log_file;

// Lista para almacenar los PIDs de los procesos hijos
pid_t procesos_hijos[MAX_PROCESOS];
int num_procesos_hijos = 0;

// Prototipo de la función registrar_operacion
void registrar_operacion(const char *operacion);

// Función para registrar un nuevo usuario
void registrar_usuario() {
    struct Cuenta nueva_cuenta;
    printf("Ingrese el nuevo número de cuenta: ");
    scanf("%d", &nueva_cuenta.numero_cuenta);
    printf("Ingrese el nombre del usuario: ");
    scanf("%49s", nueva_cuenta.nombre); // Limitar entrada para evitar desbordamiento
    printf("Ingrese el saldo inicial: ");
    scanf("%f", &nueva_cuenta.saldo);
    nueva_cuenta.transacciones = 0; // Inicializar transacciones en 0

    sem_wait(semaforo);
    FILE *archivo = fopen("data/cuentas.dat", "ab"); // Abrir en modo append binario
    if (!archivo) {
        perror("Error al abrir el archivo de cuentas");
        sem_post(semaforo);
        exit(1);
    }
    fwrite(&nueva_cuenta, sizeof(struct Cuenta), 1, archivo);
    fclose(archivo);
    sem_post(semaforo);
    printf("Usuario registrado exitosamente.\n");

    // Registrar en el log
    char mensaje[100];
    sprintf(mensaje, "Usuario registrado: %d - %s\n", nueva_cuenta.numero_cuenta, nueva_cuenta.nombre);
    registrar_operacion(mensaje);
}

// Verifica si una cuenta existe y el nombre coincide
int verificar_login(int num_cuenta, char *nombre) {
    sem_wait(semaforo);
    FILE *archivo = fopen("data/cuentas.dat", "rb"); // Abrir en modo lectura binaria
    if (!archivo) {
        perror("Error al abrir el archivo de cuentas");
        sem_post(semaforo);
        return 0;
    }
    struct Cuenta cuenta;
    while (fread(&cuenta, sizeof(struct Cuenta), 1, archivo)) {
        printf("Leyendo cuenta: %d, Nombre: %s\n", cuenta.numero_cuenta, cuenta.nombre); // Depuración
        if (cuenta.numero_cuenta == num_cuenta && strcmp(cuenta.nombre, nombre) == 0) {
            fclose(archivo);
            sem_post(semaforo);
            return 1; // Cuenta y nombre coinciden
        }
    }
    fclose(archivo);
    sem_post(semaforo);
    return 0; // No se encontró coincidencia
}

void leer_configuracion(const char *ruta) {
    FILE *file = fopen(ruta, "r");
    if (!file) {
        perror("Error al abrir config.txt");
        exit(1);
    }
    char linea[100];
    while (fgets(linea, sizeof(linea), file)) {
        if (strstr(linea, "LIMITE_RETIRO")) sscanf(linea, "LIMITE_RETIRO = %d", &config.limite_retiro);
        else if (strstr(linea, "LIMITE_TRANSFERENCIA")) sscanf(linea, "LIMITE_TRANSFERENCIA = %d", &config.limite_transferencia);
        else if (strstr(linea, "UMBRAL_RETIROS")) sscanf(linea, "UMBRAL_RETIROS = %d", &config.umbral_retiros);
        else if (strstr(linea, "UMBRAL_TRANSFERENCIAS")) sscanf(linea, "UMBRAL_TRANSFERENCIAS = %d", &config.umbral_transferencias);
        else if (strstr(linea, "NUM_HILOS")) sscanf(linea, "NUM_HILOS = %d", &config.num_hilos);
        else if (strstr(linea, "ARCHIVO_CUENTAS")) sscanf(linea, "ARCHIVO_CUENTAS = %s", config.archivo_cuentas);
        else if (strstr(linea, "ARCHIVO_LOG")) sscanf(linea, "ARCHIVO_LOG = %s", config.archivo_log);
    }
    fclose(file);
}

void inicializar_logger() {
    log_file = fopen(config.archivo_log, "a");
    if (!log_file) {
        perror("Error al abrir el log");
        exit(1);
    }
    registrar_operacion("Logger inicializado\n");
}

void registrar_operacion(const char *operacion) {
    time_t ahora = time(NULL);
    char *timestamp = ctime(&ahora);
    timestamp[strlen(timestamp) - 1] = '\0'; // Quitar el '\n'
    fprintf(log_file, "[%s] %s", timestamp, operacion);
    fflush(log_file);
}

void mostrar_menu(void) {
    printf("**\n");
    printf("*       MENÚ DE AUTENTICACIÓN          *\n");
    printf("**\n");
    printf("* 1. Log-In                            *\n");
    printf("* 2. Registrarse                       *\n");
    printf("* 3. Salir                             *\n");
    printf("**\n");
    printf("Elija una opción: ");
}

void cerrar_logger() {
    registrar_operacion("Logger cerrado\n");
    fclose(log_file);
}

// Función para terminar todos los procesos hijos y cerrar las terminales
void terminar_procesos_hijos() {
    // Enviar señal SIGTERM a los procesos hijos almacenados
    for (int i = 0; i < num_procesos_hijos; i++) {
        if (procesos_hijos[i] > 0) {
            kill(procesos_hijos[i], SIGTERM);
            waitpid(procesos_hijos[i], NULL, 0);
        }
    }

    // Usar pkill para cerrar todas las instancias de gnome-terminal abiertas por este proceso
    // Nota: Esto asume que no hay otras instancias de gnome-terminal que no estén relacionadas con banco.c
    system("pkill -u $USER gnome-terminal");

    num_procesos_hijos = 0; // Reiniciar el contador
}

int main() {
    // Leer configuración y inicializar logger
    leer_configuracion("config.txt");
    inicializar_logger();

    // Crear semáforo
    semaforo = sem_open("/cuentas_sem", O_CREAT, 0644, 1);
    if (semaforo == SEM_FAILED) {
        perror("Error al crear semáforo");
        cerrar_logger();
        exit(1);
    }

    // Lanzar monitor en una nueva terminal
    pid_t pid_monitor = fork();
    if (pid_monitor == 0) { // Proceso hijo
        execlp("gnome-terminal", "gnome-terminal", "--", "./monitor", NULL);
        perror("Error al ejecutar gnome-terminal para monitor");
        exit(1);
    } else if (pid_monitor < 0) {
        perror("Error en fork para monitor");
        cerrar_logger();
        exit(1);
    } else {
        // Almacenar el PID del proceso hijo (monitor)
        if (num_procesos_hijos < MAX_PROCESOS) {
            procesos_hijos[num_procesos_hijos++] = pid_monitor;
        }
    }

    // Bucle principal
    while (1) {
        mostrar_menu();
        int opcion;
        scanf("%d", &opcion);
        while (getchar() != '\n'); // Limpiar buffer

        switch (opcion) {
            case 1: { // Log-In
                int num_cuenta_autenticado;
                char nombre[50];
                printf("Ingrese su número de cuenta: ");
                scanf("%d", &num_cuenta_autenticado);
                printf("Ingrese su nombre: ");
                scanf("%49s", nombre);

                if (verificar_login(num_cuenta_autenticado, nombre)) {
                    printf("Inicio de sesión exitoso.\n");

                    // Registrar el inicio de sesión en el log
                    char mensaje[100];
                    sprintf(mensaje, "Inicio de sesión exitoso: Cuenta %d\n", num_cuenta_autenticado);
                    registrar_operacion(mensaje);

                    // Crear proceso hijo para ejecutar usuario.c
                    pid_t pid = fork();
                    if (pid == 0) { // Proceso hijo
                        char num_cuenta_str[10];
                        sprintf(num_cuenta_str, "%d", num_cuenta_autenticado);
                        // Pasar el nombre del archivo de log a usuario.c
                        execlp("gnome-terminal", "gnome-terminal", "--", "./usuario", num_cuenta_str, config.archivo_log, NULL);
                        perror("Error al ejecutar gnome-terminal");
                        exit(1);
                    } else if (pid < 0) {
                        perror("Error en fork");
                        cerrar_logger();
                        exit(1);
                    } else {
                        // Almacenar el PID del proceso hijo (usuario)
                        if (num_procesos_hijos < MAX_PROCESOS) {
                            procesos_hijos[num_procesos_hijos++] = pid;
                        }
                    }
                    // El proceso padre no espera, sigue ejecutándose
                } else {
                    printf("Número de cuenta o nombre incorrectos.\n");
                }
                break;
            }

            case 2: // Registrarse
                registrar_usuario();
                break;

            case 3: // Salir
                printf("Saliendo...\n");
                // Terminar todos los procesos hijos y cerrar las terminales
                terminar_procesos_hijos();
                // Destruir ambos semáforos
                sem_unlink("/cuentas_sem"); // Destruir semáforo de cuentas
                sem_unlink("/log_sem");     // Destruir semáforo de log
                sem_close(semaforo);
                cerrar_logger(); // Cerrar logger
                exit(0);

            default:
                printf("Opción no válida.\n");
                break;
        }
    }

    return 0;
}
