#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <fcntl.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>

#define MAX_USERS 1

typedef struct {
    int limite_retiro;
    int limite_transferencia;
    int umbral_retiros;
    int umbral_transferencias;
    int num_hilos;
    char archivo_cuentas[50];
    char archivo_log[50];
} Config;

Config config;
sem_t *semaforo;
FILE *log_file;

// Estructura para una cuenta
struct Cuenta {
    int numero_cuenta;
    char nombre[50]; // Tamaño máximo para el nombre
    float saldo;
    int transacciones; // Contador de transacciones
};

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
}

void registrar_operacion(const char *operacion) {
    time_t ahora = time(NULL);
    char *timestamp = ctime(&ahora);
    timestamp[strlen(timestamp) - 1] = '\0'; // Quitar el '\n'
    fprintf(log_file, "[%s] %s", timestamp, operacion);
    fflush(log_file);
}

void mostrar_menu(void) {
    printf("****************************************\n");
    printf("*       MENÚ DE AUTENTICACIÓN          *\n");
    printf("****************************************\n");
    printf("* 1. Log-In                            *\n");
    printf("* 2. Registrarse                       *\n");
    printf("* 3. Salir                             *\n");
    printf("****************************************\n");
    printf("Elija una opción: ");
}

void cerrar_logger() {
    fclose(log_file);
}

int main() {
    // Leer configuración y inicializar logger
    leer_configuracion("config.txt");
    inicializar_logger();

    // Crear semáforo
    semaforo = sem_open("/cuentas_sem", O_CREAT, 0644, 1);
    if (semaforo == SEM_FAILED) {
        perror("Error al crear semáforo");
        exit(1);
    }

    int autenticado = 0;
    int num_cuenta_autenticado;

    // Menú de autenticación
    while (!autenticado) {
        mostrar_menu();
        int opcion;
        scanf("%d", &opcion);
        while (getchar() != '\n'); // Limpiar buffer

        switch (opcion) {
            case 1: // Log-In
                printf("Ingrese su número de cuenta: ");
                scanf("%d", &num_cuenta_autenticado);
                char nombre[50];
                printf("Ingrese su nombre: ");
                scanf("%49s", nombre);
                if (verificar_login(num_cuenta_autenticado, nombre)) {
                    autenticado = 1;
                    printf("Inicio de sesión exitoso.\n");

                    // Lanzar usuario.c en una nueva terminal con gnome-terminal
                    pid_t pid = fork();
                    if (pid == 0) {
                        char num_cuenta_str[10];
                        sprintf(num_cuenta_str, "%d", num_cuenta_autenticado);
                        execlp("gnome-terminal", "gnome-terminal", "--", "./usuario", num_cuenta_str, NULL);
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

            case 2: // Registrarse
                registrar_usuario();
                break;

            case 3: // Salir
                sem_unlink("/cuentas_sem");
                sem_close(semaforo);
                cerrar_logger();
                printf("Saliendo...\n");
                exit(0);

            default:
                printf("Opción no válida.\n");
                break;
        }
    }

    sem_close(semaforo);
    cerrar_logger();
    return 0;
}
