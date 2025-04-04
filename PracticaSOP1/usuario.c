#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

// Semáforo para controlar el acceso concurrente
sem_t *semaforo;

// Estructura para las cuentas
struct Cuenta {
    int numero_cuenta;
    char nombre[50]; // Nombre del usuario
    float saldo;
    int transacciones; // Contador de transacciones
};

// Estructura para las operaciones
struct Operacion {
    int tipo; // 1: Depósito, 2: Retiro, 3: Transferencia, 4: Consulta de saldo
    int num_cuenta;
    float monto;
    int num_cuenta_destino; // Solo para transferencia
};

// Función que muestra el menú principal con un formato mejorado
void mostrar_menu(void) {
    printf("****************************************\n");
    printf("*          SISTEMA BANCARIO            *\n");
    printf("****************************************\n");
    printf("* 1. Depósito                          *\n");
    printf("* 2. Retiro                            *\n");
    printf("* 3. Transferencia                     *\n");
    printf("* 4. Consultar saldo                   *\n");
    printf("* 5. Salir                             *\n");
    printf("****************************************\n");
    printf("Elija una opción: ");
}

// Función que realiza la operación bancaria
void *realizar_operacion(void *arg) {
    struct Operacion *op = (struct Operacion *)arg;
    sem_wait(semaforo); // Bloquear acceso al archivo
    FILE *archivo = fopen("data/cuentas.dat", "rb+");
    if (!archivo) {
        perror("Error al abrir cuentas.dat");
        sem_post(semaforo);
        exit(1);
    }

    struct Cuenta cuenta;
    char mensaje_operacion[150] = "Operación no realizada.";
    int encontrada = 0;

    if (op->tipo == 1 || op->tipo == 2 || op->tipo == 3) { // Depósito, Retiro, Transferencia
        while (fread(&cuenta, sizeof(struct Cuenta), 1, archivo)) {
            if (cuenta.numero_cuenta == op->num_cuenta) {
                encontrada = 1;
                long pos = ftell(archivo) - sizeof(struct Cuenta);
                if (op->tipo == 1) { // Depósito
                    cuenta.saldo += op->monto;
                    cuenta.transacciones++; // Incrementar transacciones
                    sprintf(mensaje_operacion, "Depósito realizado. Nuevo saldo: %.2f", cuenta.saldo);
                } else if (op->tipo == 2) { // Retiro
                    if (cuenta.saldo >= op->monto) {
                        cuenta.saldo -= op->monto;
                        cuenta.transacciones++; // Incrementar transacciones
                        sprintf(mensaje_operacion, "Retiro realizado. Nuevo saldo: %.2f", cuenta.saldo);
                    } else {
                        sprintf(mensaje_operacion, "Saldo insuficiente para el retiro.");
                        break;
                    }
                } else if (op->tipo == 3) { // Transferencia
                    struct Cuenta cuenta_destino;
                    int encontrada_destino = 0;
                    long pos_destino = 0;
                    rewind(archivo);
                    while (fread(&cuenta_destino, sizeof(struct Cuenta), 1, archivo)) {
                        if (cuenta_destino.numero_cuenta == op->num_cuenta_destino) {
                            encontrada_destino = 1;
                            pos_destino = ftell(archivo) - sizeof(struct Cuenta);
                            break;
                        }
                    }
                    if (encontrada_destino && cuenta.saldo >= op->monto) {
                        cuenta.saldo -= op->monto;
                        cuenta_destino.saldo += op->monto;
                        cuenta.transacciones++; // Incrementar transacciones origen
                        cuenta_destino.transacciones++; // Incrementar transacciones destino
                        fseek(archivo, pos, SEEK_SET);
                        fwrite(&cuenta, sizeof(struct Cuenta), 1, archivo);
                        fseek(archivo, pos_destino, SEEK_SET);
                        fwrite(&cuenta_destino, sizeof(struct Cuenta), 1, archivo);
                        sprintf(mensaje_operacion, "Transferencia realizada.");
                    } else {
                        sprintf(mensaje_operacion, "Transferencia fallida: cuenta destino no encontrada o saldo insuficiente.");
                    }
                    break;
                }
                if (op->tipo != 3) {
                    fseek(archivo, pos, SEEK_SET);
                    fwrite(&cuenta, sizeof(struct Cuenta), 1, archivo);
                }
                break;
            }
        }
        if (!encontrada) {
            sprintf(mensaje_operacion, "Cuenta no encontrada.");
        }
    } else if (op->tipo == 4) { // Consulta de saldo
        while (fread(&cuenta, sizeof(struct Cuenta), 1, archivo)) {
            if (cuenta.numero_cuenta == op->num_cuenta) {
                sprintf(mensaje_operacion, "Saldo de la cuenta %d: %.2f", cuenta.numero_cuenta, cuenta.saldo);
                break;
            }
        }
    }

    fclose(archivo);
    sem_post(semaforo); // Liberar acceso al archivo

    free(op);

    // Limpiar la terminal, mostrar el mensaje y esperar una pulsación para continuar
    system("clear");
    printf("\n%s\n\n", mensaje_operacion);
    printf("Presione Enter para continuar...");
    getchar(); // Espera a que el usuario presione Enter
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Error: Se debe proporcionar el número de cuenta.\n");
        return 1;
    }

    int num_cuenta = atoi(argv[1]); // Número de cuenta autenticado desde banco.c

    semaforo = sem_open("/cuentas_sem", 0);
    if (semaforo == SEM_FAILED) {
        perror("Error al abrir semáforo");
        exit(1);
    }

    int opcion, num_cuenta_destino;
    float monto;

    while (1) {
        system("clear");
        mostrar_menu();
        fflush(stdout);
        scanf("%d", &opcion);
        while (getchar() != '\n'); // Limpiar buffer de entrada

        if (opcion == 5) { // Salir
            sem_close(semaforo);
            printf("\nSaliendo...\n");
            exit(0);
        }

        struct Operacion *op = malloc(sizeof(struct Operacion));
        op->num_cuenta = num_cuenta;
        op->num_cuenta_destino = 0;
        op->monto = 0;
        op->tipo = opcion;

        if (opcion == 1) { // Depósito
            printf("Ingrese monto a depositar: ");
            scanf("%f", &monto);
            op->monto = monto;
        } else if (opcion == 2) { // Retiro
            printf("Ingrese monto a retirar: ");
            scanf("%f", &monto);
            op->monto = monto;
        } else if (opcion == 3) { // Transferencia
            printf("Ingrese número de cuenta destino: ");
            scanf("%d", &num_cuenta_destino);
            op->num_cuenta_destino = num_cuenta_destino;
            printf("Ingrese monto a transferir: ");
            scanf("%f", &monto);
            op->monto = monto;
        } else if (opcion == 4) {
            // Para consulta de saldo no se requiere ingreso adicional
        } else {
            printf("Opción no válida. Presione Enter para continuar...");
            getchar();
            free(op);
            continue;
        }
        while (getchar() != '\n'); // Limpiar buffer

        // Crear hilo y esperar a que termine para mostrar el menú con el resultado
        pthread_t hilo_operacion;
        pthread_create(&hilo_operacion, NULL, realizar_operacion, op);
        pthread_join(hilo_operacion, NULL);
    }

    return 0;
}
