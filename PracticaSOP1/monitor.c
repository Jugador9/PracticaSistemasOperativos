#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#define MAX_LINE 1024
#define MAX_CUENTAS 10 // Límite de cuentas a monitorear

// Estructura para la configuración desde config.txt
typedef struct {
    int limite_retiro;
    int limite_transferencia;
    int umbral_retiros;
    int umbral_transferencias;
    int num_hilos;
    char archivo_cuentas[50];
    char archivo_log[50];
} Config;

// Estructura para rastrear operaciones por cuenta
typedef struct {
    int num_cuenta;
    int retiros_consecutivos;
    int transferencias_consecutivas;
} CuentaMonitor;

// Leer configuración desde config.txt
void leer_configuracion(const char *ruta, Config *config) {
    FILE *file = fopen(ruta, "r");
    if (!file) {
        perror("Error al abrir config.txt");
        exit(1);
    }
    char linea[100];
    while (fgets(linea, sizeof(linea), file)) {
        if (strstr(linea, "LIMITE_RETIRO")) sscanf(linea, "LIMITE_RETIRO = %d", &config->limite_retiro);
        else if (strstr(linea, "LIMITE_TRANSFERENCIA")) sscanf(linea, "LIMITE_TRANSFERENCIA = %d", &config->limite_transferencia);
        else if (strstr(linea, "UMBRAL_RETIROS")) sscanf(linea, "UMBRAL_RETIROS = %d", &config->umbral_retiros);
        else if (strstr(linea, "UMBRAL_TRANSFERENCIAS")) sscanf(linea, "UMBRAL_TRANSFERENCIAS = %d", &config->umbral_transferencias);
        else if (strstr(linea, "NUM_HILOS")) sscanf(linea, "NUM_HILOS = %d", &config->num_hilos);
        else if (strstr(linea, "ARCHIVO_CUENTAS")) sscanf(linea, "ARCHIVO_CUENTAS = %s", config->archivo_cuentas);
        else if (strstr(linea, "ARCHIVO_LOG")) sscanf(linea, "ARCHIVO_LOG = %s", config->archivo_log);
    }
    fclose(file);
}

// Buscar o añadir cuenta al monitoreo
int buscar_o_agregar_cuenta(CuentaMonitor *cuentas, int *num_cuentas, int num_cuenta) {
    for (int i = 0; i < *num_cuentas; i++) {
        if (cuentas[i].num_cuenta == num_cuenta) {
            return i;
        }
    }
    if (*num_cuentas < MAX_CUENTAS) {
        cuentas[*num_cuentas].num_cuenta = num_cuenta;
        cuentas[*num_cuentas].retiros_consecutivos = 0;
        cuentas[*num_cuentas].transferencias_consecutivas = 0;
        return (*num_cuentas)++;
    }
    return -1; // Límite de cuentas alcanzado
}

int main() {
    Config config;
    leer_configuracion("config.txt", &config);

    // Estructura para rastrear operaciones
    CuentaMonitor cuentas[MAX_CUENTAS] = {0};
    int num_cuentas = 0;

    // Abrir archivo de log para lectura
    FILE *log_file = fopen(config.archivo_log, "r");
    if (!log_file) {
        perror("Error al abrir transacciones.log");
        exit(1);
    }

    // Posicionarse al final del archivo para leer nuevas entradas
    fseek(log_file, 0, SEEK_END);
    long ultima_pos = ftell(log_file);

    printf("Monitor iniciado. Leyendo %s en tiempo real...\n", config.archivo_log);

    // Bucle para monitorear el archivo en tiempo real
    while (1) {
        char linea[MAX_LINE];
        if (fgets(linea, MAX_LINE, log_file) != NULL) {
            int num_cuenta, num_cuenta_destino;
            float monto;
            char operacion[50];

            // Parsear líneas relevantes (Retiro o Transferencia)
            if (sscanf(linea, "[%*[^]]] Retiro exitoso de %f en cuenta %d", &monto, &num_cuenta) == 2) {
                int idx = buscar_o_agregar_cuenta(cuentas, &num_cuentas, num_cuenta);
                if (idx == -1) {
                    printf("Límite de cuentas monitoreadas alcanzado.\n");
                    continue;
                }
                cuentas[idx].retiros_consecutivos++;
                cuentas[idx].transferencias_consecutivas = 0; // Reiniciar transferencias
                if (cuentas[idx].retiros_consecutivos > config.umbral_retiros) {
                    printf("ALERTA: Detectada anomalía - %d retiros consecutivos en cuenta %d (umbral: %d)\n",
                           cuentas[idx].retiros_consecutivos, num_cuenta, config.umbral_retiros);
                }
            }
            else if (sscanf(linea, "[%*[^]]] Transferencia exitosa de %f de cuenta %d a cuenta %d", 
                            &monto, &num_cuenta, &num_cuenta_destino) == 3) {
                int idx = buscar_o_agregar_cuenta(cuentas, &num_cuentas, num_cuenta);
                if (idx == -1) {
                    printf("Límite de cuentas monitoreadas alcanzado.\n");
                    continue;
                }
                cuentas[idx].transferencias_consecutivas++;
                cuentas[idx].retiros_consecutivos = 0; // Reiniciar retiros
                if (cuentas[idx].transferencias_consecutivas > config.umbral_transferencias) {
                    printf("ALERTA: Detectada anomalía - %d transferencias consecutivas en cuenta %d (umbral: %d)\n",
                           cuentas[idx].transferencias_consecutivas, num_cuenta, config.umbral_transferencias);
                }
            }
            ultima_pos = ftell(log_file);
        } else {
            // Si no hay nuevas líneas, verificar el estado del archivo
            clearerr(log_file);
            usleep(100000); // Esperar 100ms
            struct stat statbuf;
            if (stat(config.archivo_log, &statbuf) == -1) {
                perror("Error al verificar el estado del archivo de log");
                fclose(log_file);
                exit(1);
            }
            off_t tamano_actual = statbuf.st_size;
            if (tamano_actual < ultima_pos) { // Si el archivo fue truncado
                fclose(log_file);
                log_file = fopen(config.archivo_log, "r");
                if (!log_file) {
                    perror("Error al reabrir transacciones.log");
                    exit(1);
                }
                fseek(log_file, 0, SEEK_SET);
                ultima_pos = 0;
            } else if (tamano_actual > ultima_pos) {
                fseek(log_file, ultima_pos, SEEK_SET);
            }
        }
    }
}
