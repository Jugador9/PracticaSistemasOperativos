#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/msg.h>

// Estructura para mensajes de la cola
struct msgbuf {
    long tipo;
    char texto[256];
};

// Semáforo para control de acceso concurrente
sem_t *semaforo;
// Mutex para sincronización adicional
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Estructura para las cuentas
struct Cuenta {
    int numero_cuenta;
    char nombre[50];
    float saldo;
    int transacciones;
};

// Estructura para las operaciones
struct Operacion {
    int tipo; // 1: Depósito, 2: Retiro, 3: Transferencia, 4: Consulta de saldo
    int num_cuenta;
    float monto;
    int num_cuenta_destino;
    int msqid; // Identificador de la cola de mensajes
};

// Estructura para la configuración
typedef struct {
    int limite_retiro;
    int limite_transferencia;
    int umbral_retiros;
    int umbral_transferencias;
    int num_hilos;
    char archivo_cuentas[50];
    char archivo_log[50];
} Config;

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

// Registra una transacción en el log y la envía a la cola de mensajes
void registrar_transaccion(int tipo, int num_cuenta, float monto, int num_cuenta_destino, int msqid) {
    time_t ahora = time(NULL);
    struct tm *tm_info = localtime(&ahora);
    char timestamp[20];
    strftime(timestamp, 20, "%Y-%m-%d %H:%M:%S", tm_info);

    char operacion[20];
    switch (tipo) {
        case 1: strcpy(operacion, "Depósito"); break;
        case 2: strcpy(operacion, "Retiro"); break;
        case 3: strcpy(operacion, "Transferencia"); break;
        case 4: strcpy(operacion, "Consulta de saldo"); break;
        default: strcpy(operacion, "Desconocida"); break;
    }

    char mensaje[256];
    if (tipo == 3) {
        sprintf(mensaje, "[%s] Operación: %s, Cuenta origen: %d, Cuenta destino: %d, Monto: %.2f\n",
                timestamp, operacion, num_cuenta, num_cuenta_destino, monto);
    } else {
        sprintf(mensaje, "[%s] Operación: %s, Cuenta: %d, Monto: %.2f\n",
                timestamp, operacion, num_cuenta, monto);
    }

    // Escribir en data/transacciones.log
    FILE *log_file = fopen("data/transacciones.log", "a");
    if (log_file == NULL) {
        perror("Error al abrir transacciones.log");
    } else {
        fprintf(log_file, "%s", mensaje);
        fclose(log_file);
    }

    // Enviar a la cola de mensajes
    struct msgbuf msg;
    msg.tipo = 1; // Tipo de mensaje arbitrario
    strncpy(msg.texto, mensaje, sizeof(msg.texto) - 1);
    msg.texto[sizeof(msg.texto) - 1] = '\0';
    if (msgsnd(msqid, &msg, sizeof(msg.texto), 0) == -1) {
        perror("Error al enviar mensaje a la cola");
    }
}

// Realiza una operación bancaria
void *realizar_operacion(void *arg) {
    struct Operacion *op = (struct Operacion *)arg;
    Config configuracion = leer_configuracion("config.txt");

    pthread_mutex_lock(&mutex);
    sem_wait(semaforo);

    FILE *archivo = fopen("data/cuentas.dat", "rb+");
    if (!archivo) {
        perror("Error al abrir cuentas.dat");
        sem_post(semaforo);
        pthread_mutex_unlock(&mutex);
        exit(1);
    }

    struct Cuenta cuenta;
    char mensaje_operacion[150] = "Operación no realizada.";
    int encontrada = 0;

    if (op->tipo == 1 || op->tipo == 2 || op->tipo == 3) {
        while (fread(&cuenta, sizeof(struct Cuenta), 1, archivo)) {
            if (cuenta.numero_cuenta == op->num_cuenta) {
                encontrada = 1;
                long pos = ftell(archivo) - sizeof(struct Cuenta);
                if (op->tipo == 1) {
                    cuenta.saldo += op->monto;
                    cuenta.transacciones++;
                    sprintf(mensaje_operacion, "Depósito realizado. Nuevo saldo: %.2f", cuenta.saldo);
                    registrar_transaccion(1, op->num_cuenta, op->monto, 0, op->msqid);
                } else if (op->tipo == 2) {
                    if (op->monto > configuracion.limite_retiro) {
                        sprintf(mensaje_operacion, "Error: El monto del retiro supera el límite permitido de %.2f", (float)configuracion.limite_retiro);
                        break;
                    }
                    if (cuenta.saldo >= op->monto) {
                        cuenta.saldo -= op->monto;
                        cuenta.transacciones++;
                        sprintf(mensaje_operacion, "Retiro realizado. Nuevo saldo: %.2f", cuenta.saldo);
                        registrar_transaccion(2, op->num_cuenta, op->monto, 0, op->msqid);
                    } else {
                        sprintf(mensaje_operacion, "Saldo insuficiente para el retiro.");
                        break;
                    }
                } else if (op->tipo == 3) {
                    if (op->monto > configuracion.limite_transferencia) {
                        sprintf(mensaje_operacion, "Error: El monto de la transferencia supera el límite permitido de %.2f", (float)configuracion.limite_transferencia);
                        break;
                    }
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
                        cuenta.transacciones++;
                        cuenta_destino.transacciones++;
                        fseek(archivo, pos, SEEK_SET);
                        fwrite(&cuenta, sizeof(struct Cuenta), 1, archivo);
                        fseek(archivo, pos_destino, SEEK_SET);
                        fwrite(&cuenta_destino, sizeof(struct Cuenta), 1, archivo);
                        sprintf(mensaje_operacion, "Transferencia realizada.");
                        registrar_transaccion(3, op->num_cuenta, op->monto, op->num_cuenta_destino, op->msqid);
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
    } else if (op->tipo == 4) {
        while (fread(&cuenta, sizeof(struct Cuenta), 1, archivo)) {
            if (cuenta.numero_cuenta == op->num_cuenta) {
                sprintf(mensaje_operacion, "Saldo de la cuenta %d: %.2f", cuenta.numero_cuenta, cuenta.saldo);
                registrar_transaccion(4, op->num_cuenta, 0, 0, op->msqid);
                break;
            }
        }
    }

    fclose(archivo);
    sem_post(semaforo);
    pthread_mutex_unlock(&mutex);

    free(op);

    system("clear");
    printf("\n%s\n\n", mensaje_operacion);
    printf("Presione Enter para continuar...");
    getchar();
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Error: Se debe proporcionar el número de cuenta y el identificador de la cola de mensajes.\n");
        return 1;
    }

    int num_cuenta = atoi(argv[1]);
    int msqid = atoi(argv[2]);

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
        while (getchar() != '\n');

        if (opcion == 5) {
            sem_close(semaforo);
            printf("\nSaliendo...\n");
            exit(0);
        }

        struct Operacion *op = malloc(sizeof(struct Operacion));
        op->num_cuenta = num_cuenta;
        op->num_cuenta_destino = 0;
        op->monto = 0;
        op->tipo = opcion;
        op->msqid = msqid;

        if (opcion == 1) {
            printf("Ingrese monto a depositar: ");
            scanf("%f", &monto);
            op->monto = monto;
        } else if (opcion == 2) {
            printf("Ingrese monto a retirar: ");
            scanf("%f", &monto);
            op->monto = monto;
        } else if (opcion == 3) {
            printf("Ingrese número de cuenta destino: ");
            scanf("%d", &num_cuenta_destino);
            op->num_cuenta_destino = num_cuenta_destino;
            printf("Ingrese monto a transferir: ");
            scanf("%f", &monto);
            op->monto = monto;
        } else if (opcion == 4) {
            // Consulta de saldo no requiere entrada adicional
        } else {
            printf("Opción no válida. Presione Enter para continuar...");
            getchar();
            free(op);
            continue;
        }
        while (getchar() != '\n');

        pthread_t hilo_operacion;
        pthread_create(&hilo_operacion, NULL, realizar_operacion, op);
        pthread_join(hilo_operacion, NULL);
    }

    return 0;
}
