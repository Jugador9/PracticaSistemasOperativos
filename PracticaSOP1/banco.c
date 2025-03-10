#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <fcntl.h>
#include <string.h>

#define MAX_USERS 5

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

void cerrar_logger() {
    fclose(log_file);
}

int main() {
    leer_configuracion("config.txt");
    inicializar_logger();
    semaforo = sem_open("/cuentas_sem", O_CREAT, 0644, 1);
    if (semaforo == SEM_FAILED) {
        perror("Error al crear semáforo");
        exit(1);
    }

    for (int i = 0; i < MAX_USERS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            execl("./usuario", "usuario", NULL);
            perror("Error al ejecutar usuario.c");
            exit(1);
        }
    }

    for (int i = 0; i < MAX_USERS; i++) {
        wait(NULL);
    }

    sem_unlink("/cuentas_sem");
    sem_close(semaforo);
    cerrar_logger();
    return 0;
}
