#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <fcntl.h>

// Estructura para una cuenta (igual que en banco.c)
struct Cuenta {
    int numero_cuenta;
    char nombre[50];
    float saldo;
    int transacciones;
};

// Función para inicializar cuentas predefinidas
void inicializar_cuentas(sem_t *semaforo) {
    struct Cuenta cuentas[] = {
        {1001, "Juan Pérez", 5000.00, 0},
        {1002, "María López", 3000.00, 0},
        {1003, "Carlos Gómez", 7000.00, 0},
        {1004, "Ana Martínez", 4000.00, 0}
    };
    int num_cuentas = sizeof(cuentas) / sizeof(cuentas[0]);

    sem_wait(semaforo); // Bloquear acceso al archivo
    FILE *archivo = fopen("data/cuentas.dat", "ab"); // Abrir en modo append binario
    if (!archivo) {
        perror("Error al abrir/crear data/cuentas.dat");
        sem_post(semaforo);
        exit(1);
    }

    // Escribir cada cuenta en el archivo
    for (int i = 0; i < num_cuentas; i++) {
        fwrite(&cuentas[i], sizeof(struct Cuenta), 1, archivo);
    }

    fclose(archivo);
    sem_post(semaforo); // Liberar acceso
    printf("Archivo data/cuentas.dat inicializado con %d cuentas predefinidas.\n", num_cuentas);
}

int main() {
    // Crear semáforo (igual que en banco.c)
    sem_t *semaforo = sem_open("/cuentas_sem", O_CREAT, 0644, 1);
    if (semaforo == SEM_FAILED) {
        perror("Error al crear semáforo");
        exit(1);
    }

    // Inicializar cuentas
    inicializar_cuentas(semaforo);

    // Limpiar semáforo
    sem_close(semaforo);
    // No eliminamos el semáforo aquí para que banco.c pueda usarlo
    // sem_unlink("/cuentas_sem");

    return 0;
}
