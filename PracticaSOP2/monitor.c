#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define MAX_LINE 256

// Estructura para mensajes de la cola
struct msgbuf {
    long tipo;
    char texto[MAX_LINE];
};

// Estructura para operaciones recientes
typedef struct {
    int cuenta;
    time_t timestamp;
    char operacion[20];
} OperacionReciente;

OperacionReciente operaciones[1000];
int num_operaciones = 0;

// Elimina espacios iniciales de una cadena
void trim_leading(char *str) {
    int i = 0, j = 0;
    while (str[i] == ' ') i++;
    while (str[i]) str[j++] = str[i++];
    str[j] = '\0';
}

// Elimina espacios y caracteres finales (\n) de una cadena
void trim_trailing(char *str) {
    int len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == ' ')) {
        str[len - 1] = '\0';
        len--;
    }
}

// Analiza una línea de transacción y genera alertas si es necesario
void analizar_linea(char *linea, int pipe_write_fd, int limite_retiro, int limite_transferencia, int umbral_retiros, int umbral_transferencias) {
    char linea_copia[MAX_LINE];
    strncpy(linea_copia, linea, MAX_LINE - 1);
    linea_copia[MAX_LINE - 1] = '\0';

    // Extraer timestamp
    char *timestamp_str = strtok(linea_copia, "]");
    if (!timestamp_str) {
        printf("Error: No se pudo extraer timestamp\n");
        return;
    }
    timestamp_str++; // Saltar el '[' inicial
    struct tm tm;
    if (strptime(timestamp_str, "%Y-%m-%d %H:%M:%S", &tm) == NULL) {
        printf("Error: Fallo al parsear timestamp: %s\n", timestamp_str);
        return;
    }
    time_t timestamp = mktime(&tm);

    // Extraer operación
    char *operacion_str = strstr(linea, "Operación: ");
    if (!operacion_str) {
        printf("Error: No se encontró 'Operación: ' en la línea\n");
        return;
    }
    operacion_str += 11; // Avanzar después de "Operación: "
    trim_trailing(operacion_str); // Eliminar \n o espacios finales
    printf("Depuración: operacion_str = '%s'\n", operacion_str); // Mostrar contenido exacto

    char operacion[20];
    if (sscanf(operacion_str, "%19[^,]", operacion) != 1) {
        printf("Error: No se pudo parsear operación\n");
        return;
    }
    trim_leading(operacion);
    printf("Depuración: operacion = '%s'\n", operacion);

    // Extraer cuenta y monto
    int cuenta = 0, cuenta_destino = 0;
    float monto = 0.0;
    if (strcmp(operacion, "Transferencia") == 0) {
        // Parsear transferencia con formato más flexible
        char *cuenta_origen_str = strstr(operacion_str, "Cuenta origen: ");
        char *cuenta_destino_str = strstr(operacion_str, "Cuenta destino: ");
        char *monto_str = strstr(operacion_str, "Monto: ");
        if (!cuenta_origen_str || !cuenta_destino_str || !monto_str) {
            printf("Error: No se encontraron todos los campos de transferencia\n");
            return;
        }
        cuenta_origen_str += 15; // Avanzar después de "Cuenta origen: "
        cuenta_destino_str += 16; // Avanzar después de "Cuenta destino: "
        monto_str += 7; // Avanzar después de "Monto: "
        if (sscanf(cuenta_origen_str, "%d", &cuenta) != 1 ||
            sscanf(cuenta_destino_str, "%d", &cuenta_destino) != 1 ||
            sscanf(monto_str, "%f", &monto) != 1) {
            printf("Error: No se pudo parsear transferencia (cuenta=%d, destino=%d, monto=%f)\n", cuenta, cuenta_destino, monto);
            return;
        }
        printf("Depuración: Transferencia parseada - Cuenta: %d, Destino: %d, Monto: %.2f\n", cuenta, cuenta_destino, monto);
    } else {
        if (sscanf(operacion_str, "%*[^,], Cuenta: %d, Monto: %f", &cuenta, &monto) != 2) {
            printf("Error: No se pudo parsear operación no transferencia\n");
            return;
        }
        printf("Depuración: Operación parseada - Cuenta: %d, Monto: %.2f\n", cuenta, monto);
    }

    // Generar alertas para límites
    char alerta[256];
    if (strcmp(operacion, "Retiro") == 0 && monto > limite_retiro) {
        sprintf(alerta, "ALERTA: Retiro de %.2f en cuenta %d supera el límite de %d\n", monto, cuenta, limite_retiro);
        write(pipe_write_fd, alerta, strlen(alerta));
        printf("Depuración: Enviada alerta por límite de retiro\n");
    } else if (strcmp(operacion, "Transferencia") == 0 && monto > limite_transferencia) {
        sprintf(alerta, "ALERTA: Transferencia de %.2f desde cuenta %d supera el límite de %d\n", monto, cuenta, limite_transferencia);
        write(pipe_write_fd, alerta, strlen(alerta));
        printf("Depuración: Enviada alerta por límite de transferencia\n");
    }

    // Registrar operación
    operaciones[num_operaciones].cuenta = cuenta;
    operaciones[num_operaciones].timestamp = timestamp;
    strcpy(operaciones[num_operaciones].operacion, operacion);
    num_operaciones++;
    printf("Depuración: Operación registrada (%s, Cuenta: %d, Total: %d)\n", operacion, cuenta, num_operaciones);

    // Contar retiros y transferencias
    int retiros[10000] = {0};
    int transferencias[10000] = {0};
    for (int i = 0; i < num_operaciones; i++) {
        int cta = operaciones[i].cuenta;
        if (strcmp(operaciones[i].operacion, "Retiro") == 0) retiros[cta]++;
        else if (strcmp(operacion, "Transferencia") == 0) transferencias[cta]++;
    }

    // Generar alertas para umbrales
    for (int cta = 0; cta < 10000; cta++) {
        if (retiros[cta] > umbral_retiros) {
            sprintf(alerta, "ALERTA: Cuenta %d ha realizado %d retiros consecutivos (umbral: %d)\n",
                    cta, retiros[cta], umbral_retiros);
            write(pipe_write_fd, alerta, strlen(alerta));
            printf("Depuración: Enviada alerta por %d retiros en cuenta %d\n", retiros[cta], cta);
        }
        if (transferencias[cta] > umbral_transferencias) {
            sprintf(alerta, "ALERTA: Cuenta %d ha realizado %d transferencias consecutivas (umbral: %d)\n",
                    cta, transferencias[cta], umbral_transferencias);
            write(pipe_write_fd, alerta, strlen(alerta));
            printf("Depuración: Enviada alerta por %d transferencias en cuenta %d\n", transferencias[cta], cta);
        }
    }
}

int main(int argc, char *argv[]) {
    printf("Monitor iniciado\n");
    fflush(stdout);

    // Verificar argumentos
    if (argc != 7) {
        fprintf(stderr, "Error: Faltan parámetros. Uso: %s <msqid> <pipe_write_fd> <limite_retiro> <limite_transferencia> <umbral_retiros> <umbral_transferencias>\n", argv[0]);
        exit(1);
    }

    int msqid = atoi(argv[1]);
    int pipe_write_fd = atoi(argv[2]);
    int limite_retiro = atoi(argv[3]);
    int limite_transferencia = atoi(argv[4]);
    int umbral_retiros = atoi(argv[5]);
    int umbral_transferencias = atoi(argv[6]);

    printf("Parámetros recibidos: msqid=%d, pipe_write_fd=%d, limite_retiro=%d, limite_transferencia=%d, umbral_retiros=%d, umbral_transferencias=%d\n",
           msqid, pipe_write_fd, limite_retiro, limite_transferencia, umbral_retiros, umbral_transferencias);
    fflush(stdout);

    // Leer mensajes de la cola
    struct msgbuf msg;
    while (1) {
        if (msgrcv(msqid, &msg, sizeof(msg.texto), 1, 0) == -1) {
            perror("Error al recibir mensaje");
            continue;
        }
        printf("Transacción recibida: %s", msg.texto);
        analizar_linea(msg.texto, pipe_write_fd, limite_retiro, limite_transferencia, umbral_retiros, umbral_transferencias);
    }

    return 0;
}
