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
#include <sys/shm.h>

// Estructura para mensajes de la cola
struct msgbuf {
    long tipo;
    char texto[256];
};

// Estructura para una cuenta
typedef struct {
    int numero_cuenta;
    char titular[50];
    float saldo;
    int bloqueado; // 1 si la cuenta está bloqueada, 0 si está activa
} Cuenta;

// Estructura para la tabla de cuentas en memoria compartida
typedef struct {
    Cuenta cuentas[100];
    int num_cuentas;
    pthread_mutex_t mutex; // Mutex para sincronización
} TablaCuentas;

// Estructura para las operaciones
struct Operacion {
    int tipo; // 1: Depósito, 2: Retiro, 3: Transferencia, 4: Consulta de saldo
    int num_cuenta;
    float monto;
    int num_cuenta_destino;
    int msqid; // Identificador de la cola de mensajes
    TablaCuentas *tabla; // Puntero a la memoria compartida
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

// Función para leer la configuración desde config.txt
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

// Función que muestra el menú principal
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

// Función para registrar transacciones en un log por usuario y enviar a la cola de mensajes
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

    // Escribir en un archivo de log específico por usuario
    char nombre_log[100];
    sprintf(nombre_log, "data/%d/transacciones.log", num_cuenta);
    FILE *log_file = fopen(nombre_log, "a");
    if (log_file == NULL) {
        perror("Error al abrir el archivo de log del usuario");
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

// Función que realiza la operación bancaria usando memoria compartida
void *realizar_operacion(void *arg) {
    struct Operacion *op = (struct Operacion *)arg;
    Config configuracion = leer_configuracion("config.txt");

    pthread_mutex_lock(&op->tabla->mutex); // Bloquear mutex compartido

    int encontrada = 0;
    int encontrada_destino = 0;
    int idx_cuenta = -1;
    int idx_destino = -1;

    // Buscar la cuenta en la memoria compartida
    for (int i = 0; i < op->tabla->num_cuentas; i++) {
        if (op->tabla->cuentas[i].numero_cuenta == op->num_cuenta) {
            idx_cuenta = i;
            encontrada = 1;
            break;
        }
    }
    if (!encontrada) {
        printf("Cuenta no encontrada.\n");
        pthread_mutex_unlock(&op->tabla->mutex);
        free(op);
        return NULL;
    }

    char mensaje_operacion[150] = "Operación no realizada.";
    if (op->tipo == 1) { // Depósito
        op->tabla->cuentas[idx_cuenta].saldo += op->monto;
        sprintf(mensaje_operacion, "Depósito realizado. Nuevo saldo: %.2f", op->tabla->cuentas[idx_cuenta].saldo);
        registrar_transaccion(1, op->num_cuenta, op->monto, 0, op->msqid);
    } else if (op->tipo == 2) { // Retiro
        if (op->monto > configuracion.limite_retiro) {
            sprintf(mensaje_operacion, "Error: El monto del retiro supera el límite permitido de %.2f", (float)configuracion.limite_retiro);
        } else if (op->tabla->cuentas[idx_cuenta].saldo >= op->monto) {
            op->tabla->cuentas[idx_cuenta].saldo -= op->monto;
            sprintf(mensaje_operacion, "Retiro realizado. Nuevo saldo: %.2f", op->tabla->cuentas[idx_cuenta].saldo);
            registrar_transaccion(2, op->num_cuenta, op->monto, 0, op->msqid);
        } else {
            sprintf(mensaje_operacion, "Saldo insuficiente para el retiro.");
        }
    } else if (op->tipo == 3) { // Transferencia
        if (op->monto > configuracion.limite_transferencia) {
            sprintf(mensaje_operacion, "Error: El monto de la transferencia supera el límite permitido de %.2f", (float)configuracion.limite_transferencia);
        } else {
            // Buscar cuenta destino
            for (int i = 0; i < op->tabla->num_cuentas; i++) {
                if (op->tabla->cuentas[i].numero_cuenta == op->num_cuenta_destino) {
                    idx_destino = i;
                    encontrada_destino = 1;
                    break;
                }
            }
            if (encontrada_destino && op->tabla->cuentas[idx_cuenta].saldo >= op->monto) {
                op->tabla->cuentas[idx_cuenta].saldo -= op->monto;
                op->tabla->cuentas[idx_destino].saldo += op->monto;
                sprintf(mensaje_operacion, "Transferencia realizada.");
                registrar_transaccion(3, op->num_cuenta, op->monto, op->num_cuenta_destino, op->msqid);
            } else {
                sprintf(mensaje_operacion, "Transferencia fallida: cuenta destino no encontrada o saldo insuficiente.");
            }
        }
    } else if (op->tipo == 4) { // Consulta de saldo
        sprintf(mensaje_operacion, "Saldo de la cuenta %d: %.2f", op->num_cuenta, op->tabla->cuentas[idx_cuenta].saldo);
        registrar_transaccion(4, op->num_cuenta, 0, 0, op->msqid);
    }

    pthread_mutex_unlock(&op->tabla->mutex); // Desbloquear mutex

    system("clear");
    printf("\n%s\n\n", mensaje_operacion);
    printf("Presione Enter para continuar...");
    getchar();
    free(op);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Error: Se debe proporcionar el número de cuenta, el identificador de la cola de mensajes y el identificador de la memoria compartida.\n");
        return 1;
    }

    int num_cuenta = atoi(argv[1]);
    int msqid = atoi(argv[2]);
    int shm_id = atoi(argv[3]);

    // Adjuntar memoria compartida
    TablaCuentas *tabla = (TablaCuentas *) shmat(shm_id, NULL, 0);
    if (tabla == (void *) -1) {
        perror("Error al adjuntar memoria compartida");
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
            shmdt(tabla);
            printf("\nSaliendo...\n");
            exit(0);
        }

        struct Operacion *op = malloc(sizeof(struct Operacion));
        op->num_cuenta = num_cuenta;
        op->num_cuenta_destino = 0;
        op->monto = 0;
        op->tipo = opcion;
        op->msqid = msqid;
        op->tabla = tabla;

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
