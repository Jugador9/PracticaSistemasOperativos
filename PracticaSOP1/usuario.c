#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

// Semáforo para controlar el acceso concurrente
sem_t *semaforo;
sem_t *semaforo_log;

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
    const char *log_file_name; // Añadir el nombre del archivo de log
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

Config config;

// Leer configuración desde config.txt
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

// Función para registrar operaciones en el log
void registrar_operacion(const char *operacion, const char *log_file_name) {
    sem_wait(semaforo_log); // Proteger el acceso al archivo de log
    FILE *log_file = fopen(log_file_name, "a");
    if (!log_file) {
        perror("Error al abrir transacciones.log en usuario.c");
        sem_post(semaforo_log);
        return;
    }
    time_t ahora = time(NULL);
    char *timestamp = ctime(&ahora);
    timestamp[strlen(timestamp) - 1] = '\0';
    fprintf(log_file, "[%s] %s\n", timestamp, operacion);
    fflush(log_file);
    fclose(log_file);
    sem_post(semaforo_log);
}

// Función que muestra el menú principal con un formato mejorado
void mostrar_menu(void) {
    printf("******\n");
    printf("*          SISTEMA BANCARIO            *\n");
    printf("******\n");
    printf("* 1. Depósito                          *\n");
    printf("* 2. Retiro                            *\n");
    printf("* 3. Transferencia                     *\n");
    printf("* 4. Consultar saldo                   *\n");
    printf("* 5. Salir                             *\n");
    printf("******\n");
    printf("Elija una opción: ");
}

// Función que realiza la operación bancaria
void *realizar_operacion(void *arg) {
    struct Operacion *op = (struct Operacion *)arg;
    const char *log_file_name = op->log_file_name; // Obtener el nombre del archivo de log

    sem_wait(semaforo); // Bloquear acceso al archivo
    FILE *archivo = fopen("data/cuentas.dat", "rb+");
    if (!archivo) {
        perror("Error al abrir cuentas.dat");
        sem_post(semaforo);
        free(op);
        return NULL;
    }

    struct Cuenta cuenta;
    char mensaje_operacion[150] = "Operación no realizada.";
    int encontrada = 0;

    // Leer todas las cuentas para depuración
    while (fread(&cuenta, sizeof(struct Cuenta), 1, archivo)) {
        printf("Cuenta encontrada en archivo: %d, Saldo: %.2f\n", cuenta.numero_cuenta, cuenta.saldo); // Depuración
        if (cuenta.numero_cuenta == op->num_cuenta) {
            encontrada = 1;
            long pos = ftell(archivo) - sizeof(struct Cuenta);
            if (op->tipo == 1) { // Depósito
                cuenta.saldo += op->monto;
                cuenta.transacciones++; // Incrementar transacciones
                sprintf(mensaje_operacion, "Depósito exitoso de %.2f en cuenta %d. Nuevo saldo: %.2f", 
                        op->monto, op->num_cuenta, cuenta.saldo);
                fseek(archivo, pos, SEEK_SET);
                fwrite(&cuenta, sizeof(struct Cuenta), 1, archivo);
            } else if (op->tipo == 2) { // Retiro
                // Verificar el límite de retiro
                if (op->monto > config.limite_retiro) {
                    sprintf(mensaje_operacion, "Retiro fallido de %.2f en cuenta %d: Monto excede el límite de %.2f", 
                            op->monto, op->num_cuenta, (float)config.limite_retiro);
                } else if (cuenta.saldo >= op->monto) {
                    cuenta.saldo -= op->monto;
                    cuenta.transacciones++; // Incrementar transacciones
                    sprintf(mensaje_operacion, "Retiro exitoso de %.2f en cuenta %d. Nuevo saldo: %.2f", 
                            op->monto, op->num_cuenta, cuenta.saldo);
                    fseek(archivo, pos, SEEK_SET);
                    fwrite(&cuenta, sizeof(struct Cuenta), 1, archivo);
                } else {
                    sprintf(mensaje_operacion, "Retiro fallido de %.2f en cuenta %d: Saldo insuficiente (saldo actual: %.2f)", 
                            op->monto, op->num_cuenta, cuenta.saldo);
                }
            } else if (op->tipo == 3) { // Transferencia
                // Verificar el límite de transferencia
                if (op->monto > config.limite_transferencia) {
                    sprintf(mensaje_operacion, "Transferencia fallida de %.2f de cuenta %d a cuenta %d: Monto excede el límite de %.2f", 
                            op->monto, op->num_cuenta, op->num_cuenta_destino, (float)config.limite_transferencia);
                } else {
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
                        sprintf(mensaje_operacion, "Transferencia exitosa de %.2f de cuenta %d a cuenta %d", 
                                op->monto, op->num_cuenta, op->num_cuenta_destino);
                    } else if (!encontrada_destino) {
                        sprintf(mensaje_operacion, "Transferencia fallida de %.2f de cuenta %d a cuenta %d: Cuenta destino no encontrada", 
                                op->monto, op->num_cuenta, op->num_cuenta_destino);
                    } else {
                        sprintf(mensaje_operacion, "Transferencia fallida de %.2f de cuenta %d a cuenta %d: Saldo insuficiente (saldo actual: %.2f)", 
                                op->monto, op->num_cuenta, op->num_cuenta_destino, cuenta.saldo);
                    }
                }
            } else if (op->tipo == 4) { // Consulta de saldo
                sprintf(mensaje_operacion, "Saldo de la cuenta %d: %.2f", cuenta.numero_cuenta, cuenta.saldo);
            }
            break;
        }
    }
    if (!encontrada) {
        sprintf(mensaje_operacion, "Operación fallida: Cuenta %d no encontrada", op->num_cuenta);
    }

    // Registrar la operación en el log
    registrar_operacion(mensaje_operacion, log_file_name);

    fclose(archivo);
    sem_post(semaforo); // Liberar acceso al archivo

    // Limpiar la terminal, mostrar el mensaje y esperar una pulsación para continuar
    system("clear");
    printf("\n%s\n\n", mensaje_operacion);
    printf("Presione Enter para continuar...");
    getchar(); // Espera a que el usuario presione Enter

    free(op);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Error: Se debe proporcionar el número de cuenta y el nombre del archivo de log.\n");
        return 1;
    }

    int num_cuenta = atoi(argv[1]); // Número de cuenta autenticado desde banco.c
    const char *log_file_name = argv[2]; // Nombre del archivo de log

    // Leer configuración
    leer_configuracion("config.txt");

    semaforo = sem_open("/cuentas_sem", 0);
    if (semaforo == SEM_FAILED) {
        perror("Error al abrir semáforo de cuentas");
        exit(1);
    }

    semaforo_log = sem_open("/log_sem", O_CREAT, 0644, 1);
    if (semaforo_log == SEM_FAILED) {
        perror("Error al crear semáforo de log");
        sem_close(semaforo);
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
            sem_close(semaforo_log);
            printf("\nSaliendo...\n");
            exit(0);
        }

        struct Operacion *op = malloc(sizeof(struct Operacion));
        op->num_cuenta = num_cuenta;
        op->num_cuenta_destino = 0;
        op->monto = 0;
        op->tipo = opcion;
        op->log_file_name = log_file_name; // Pasar el nombre del archivo de log

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
