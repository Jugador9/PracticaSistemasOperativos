#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include "cuenta.h"  // Suponemos que contiene la definición de struct Cuenta

sem_t *semaforo;

struct Operacion {
    int tipo; // 1: Depósito, 2: Retiro, 3: Transferencia
    int num_cuenta;
    float monto;
    int num_cuenta_destino; // Para transferencia (no implementado aún)
};

void *realizar_operacion(void *arg) {
    struct Operacion *op = (struct Operacion *)arg;

    // Sección crítica: acceso al archivo
    sem_wait(semaforo);
    FILE *archivo = fopen("data/cuentas.dat", "r+b");
    if (!archivo) {
        perror("Error al abrir cuentas.dat");
        sem_post(semaforo);  // Liberar semáforo antes de salir
        exit(1);
    }

    struct Cuenta cuenta;
    while (fread(&cuenta, sizeof(struct Cuenta), 1, archivo)) {
        if (cuenta.numero_cuenta == op->num_cuenta) {
            if (op->tipo == 1) { // Depósito
                cuenta.saldo += op->monto;
            } else if (op->tipo == 2 && cuenta.saldo >= op->monto) { // Retiro
                cuenta.saldo -= op->monto;
            }
            fseek(archivo, -sizeof(struct Cuenta), SEEK_CUR);
            fwrite(&cuenta, sizeof(struct Cuenta), 1, archivo);
            break;
        }
    }
    fclose(archivo);
    sem_post(semaforo);  // Liberar semáforo después de la sección crítica

    free(op);
    return NULL;
}

int main() {
    semaforo = sem_open("/cuentas_sem", 0);
    if (semaforo == SEM_FAILED) {
        perror("Error al abrir semáforo");
        exit(1);
    }

    int opcion, num_cuenta;
    float monto;

    while (1) {
        printf("\n1. Depósito\n2. Retiro\n3. Transferencia\n4. Consultar saldo\n5. Salir\nOpción: ");
        scanf("%d", &opcion);

        switch (opcion) {
            case 1: // Depósito
            case 2: // Retiro
            case 3: // Transferencia
                printf("Número de cuenta: ");
                scanf("%d", &num_cuenta);
                printf("Monto: ");
                scanf("%f", &monto);

                struct Operacion *op = malloc(sizeof(struct Operacion));
                op->tipo = opcion;
                op->num_cuenta = num_cuenta;
                op->monto = monto;

                pthread_t hilo;
                pthread_create(&hilo, NULL, realizar_operacion, op);
                pthread_detach(hilo);
                break;

            case 4: // Consultar saldo
                printf("Número de cuenta: ");
                scanf("%d", &num_cuenta);

                sem_wait(semaforo);  // Proteger la lectura
                FILE *archivo = fopen("data/cuentas.dat", "rb");
                if (!archivo) {
                    perror("Error al abrir cuentas.dat");
                    sem_post(semaforo);
                    exit(1);
                }
                struct Cuenta cuenta;
                int encontrada = 0;
                while (fread(&cuenta, sizeof(struct Cuenta), 1, archivo)) {
                    if (cuenta.numero_cuenta == num_cuenta) {
                        printf("Saldo: %.2f\n", cuenta.saldo);
                        encontrada = 1;
                        break;
                    }
                }
                if (!encontrada) {
                    printf("Cuenta no encontrada.\n");
                }
                fclose(archivo);
                sem_post(semaforo);  // Liberar semáforo
                break;

            case 5: // Salir
                sem_close(semaforo);
                printf("Saliendo...\n");
                exit(0);

            default:
                printf("Opción no válida.\n");
                break;
        }
    }

    return 0;
}
