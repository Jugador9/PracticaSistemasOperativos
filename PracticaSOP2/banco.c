#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <fcntl.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/stat.h>

/* Definimos el número máximo de usuarios que se pueden ejecutar a la vez y las cuentas que estos pueden abrir*/
#define MAX_USERS 1
#define MAX_CUENTAS 100

/* Definimos la estructura de la cuenta, la tabla de cuentas y la configuración, así como los semáforos*/
typedef struct {
    int numero_cuenta;
    char titular[50];
    float saldo;
    int bloqueado;
} Cuenta;

typedef struct {
    Cuenta cuentas[MAX_CUENTAS];
    int num_cuentas;
    pthread_mutex_t mutex;
} TablaCuentas;

typedef struct {
    int limite_retiro;
    int limite_transferencia;
    int umbral_retiros;
    int umbral_transferencias;
    int num_hilos;
    char archivo_cuentas[50];
    char archivo_log[50];
} Config;

int pipefd[2];
int msqid;
int shm_id;
TablaCuentas *tabla;
sem_t *semaforo;
/* Desarrollamos la función que lee la configuración*/
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

/* Desarrollamos la función que iniciliza la memoria compartida y cargamos los datos desde el directorio data*/
void inicializar_memoria_compartida() {
    shm_id = shmget(IPC_PRIVATE, sizeof(TablaCuentas), IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("Error al crear memoria compartida");
        exit(1);
    }
    tabla = (TablaCuentas *) shmat(shm_id, NULL, 0);
    if (tabla == (void *) -1) {
        perror("Error al adjuntar memoria compartida");
        exit(1);
    }
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&tabla->mutex, &attr);
    tabla->num_cuentas = 0;
    FILE *archivo = fopen("data/cuentas.dat", "rb");
    if (archivo) {
        fread(tabla->cuentas, sizeof(Cuenta), MAX_CUENTAS, archivo);
        fread(&tabla->num_cuentas, sizeof(int), 1, archivo);
        fclose(archivo);
    }
}

/* Desarrollamos la función que sincroniza el disco cada 60 segundos*/
void *sincronizar_disco(void *arg) {
    while (1) {
        sleep(60);
        pthread_mutex_lock(&tabla->mutex);
        FILE *archivo = fopen("data/cuentas.dat", "wb");
        if (archivo) {
            fwrite(tabla->cuentas, sizeof(Cuenta), MAX_CUENTAS, archivo);
            fwrite(&tabla->num_cuentas, sizeof(int), 1, archivo);
            fclose(archivo);
        }
        pthread_mutex_unlock(&tabla->mutex);
    }
    return NULL;
}

/* Desarrollamos la función de registrar un usuario, donde se le piden los datos de la cuenta y la cuenta se carga a la tabla de cuentas*/
void registrar_usuario() {
    if (tabla->num_cuentas >= MAX_CUENTAS) {
        printf("Error: No se pueden registrar más cuentas.\n");
        return;
    }
    Cuenta nueva_cuenta;
    printf("Ingrese el nuevo número de cuenta: ");
    scanf("%d", &nueva_cuenta.numero_cuenta);
    printf("Ingrese el nombre del titular: ");
    scanf("%49s", nueva_cuenta.titular);
    printf("Ingrese el saldo inicial: ");
    scanf("%f", &nueva_cuenta.saldo);
    nueva_cuenta.bloqueado = 0;

    pthread_mutex_lock(&tabla->mutex);
    tabla->cuentas[tabla->num_cuentas] = nueva_cuenta;
    tabla->num_cuentas++;
    pthread_mutex_unlock(&tabla->mutex);

    /* Creamos el directorio para el usuario en caso de que no exista*/
    char path[100];
    snprintf(path, sizeof(path), "data/%d", nueva_cuenta.numero_cuenta);
    if (mkdir(path, 0777) == -1) {
        perror("Error al crear directorio del usuario");
    } else {
        printf("Directorio creado: %s\n", path);
    }
    printf("Usuario registrado exitosamente.\n");
}

/* Desarrolamos la función que verifica si el número de cuenta y el nombre son los correctos*/
int verificar_login(int num_cuenta, char *nombre) {
    pthread_mutex_lock(&tabla->mutex);
    for (int i = 0; i < tabla->num_cuentas; i++) {
        if (tabla->cuentas[i].numero_cuenta == num_cuenta && strcmp(tabla->cuentas[i].titular, nombre) == 0) {
            pthread_mutex_unlock(&tabla->mutex);
            return 1;
        }
    }
    pthread_mutex_unlock(&tabla->mutex);
    return 0;
}

/* Desarrollamos la función que muestra el menú de autenticación del usuario*/
void mostrar_menu(void) {
    printf("****************************************\n");
    printf("*       MENÚ DE AUTENTICACIÓN          *\n");
    printf("****************************************\n");
    printf("* 1. Iniciar sesión                    *\n");
    printf("* 2. Registrarse                       *\n");
    printf("* 3. Salir                             *\n");
    printf("****************************************\n");
    printf("Elija una opción: ");
}

/* Programa principal*/
int main() {
    struct stat st = {0};
    if (stat("data", &st) == -1) {
        mkdir("data", 0700);
    }

    /* LEEMOS LA CONFIGURACIÓN*/
    Config configuracion = leer_configuracion("config.txt");

    sem_unlink("/cuentas_sem");
    semaforo = sem_open("/cuentas_sem", O_CREAT, 0644, 1);
    if (semaforo == SEM_FAILED) {
        perror("Error al crear semáforo");
        exit(1);
    }

    /* INICIALIZAMOS LA MEMORIA COMPARTIDA*/
    inicializar_memoria_compartida();

    if (pipe(pipefd) == -1) {
        perror("Error al crear tubería");
        exit(1);
    }

    /* CREAMOS LA COLA DE MENSAJES*/
    key_t key = ftok("banco.c", 65);
    msqid = msgget(key, 0666 | IPC_CREAT);
    if (msqid == -1) {
        perror("Error al crear cola de mensajes");
        exit(1);
    }

    /* CREAMOS EL PROCESO MONITOR*/
    pid_t pid_monitor = fork();
    if (pid_monitor == 0) {
        close(pipefd[0]);
        char msqid_str[10];
        char pipe_write_str[10];
        char limite_retiro_str[10];
        char limite_transferencia_str[10];
        char umbral_retiros_str[10];
        char umbral_transferencias_str[10];

        sprintf(msqid_str, "%d", msqid);
        sprintf(pipe_write_str, "%d", pipefd[1]);
        sprintf(limite_retiro_str, "%d", configuracion.limite_retiro);
        sprintf(limite_transferencia_str, "%d", configuracion.limite_transferencia);
        sprintf(umbral_retiros_str, "%d", configuracion.umbral_retiros);
        sprintf(umbral_transferencias_str, "%d", configuracion.umbral_transferencias);

        execlp("gnome-terminal", "gnome-terminal", "--", "./monitor",
               msqid_str, pipe_write_str, limite_retiro_str, limite_transferencia_str,
               umbral_retiros_str, umbral_transferencias_str, NULL);
        perror("Error al ejecutar gnome-terminal para monitor");
        exit(1);
    } else if (pid_monitor < 0) {
        perror("Error en fork para monitor");
        exit(1);
    }

    /* CREAMOS EL PROCESO DE SINCRONIZACIÓN*/
    pthread_t hilo_sincronizacion;
    pthread_create(&hilo_sincronizacion, NULL, sincronizar_disco, NULL);

    
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    /* MOSTRAMOS EL MENÚ DE AUTENTICACIÓN*/
    while (1) {
        char alerta[256];
        ssize_t bytes = read(pipefd[0], alerta, sizeof(alerta) - 1);
        if (bytes > 0) {
            alerta[bytes] = '\0';
            printf("ALERTA RECIBIDA: %s", alerta);
            fflush(stdout);
        }

        mostrar_menu();
        int opcion;
        scanf("%d", &opcion);
        while (getchar() != '\n');

        switch (opcion) {
            case 1:  // INICIAR SESIÓN
            {
                int num_cuenta_autenticado;
                char nombre[50];
                printf("Ingrese su número de cuenta: ");
                scanf("%d", &num_cuenta_autenticado);
                printf("Ingrese su nombre: ");
                scanf("%49s", nombre);

                if (verificar_login(num_cuenta_autenticado, nombre)) {
                    printf("Inicio de sesión exitoso.\n");
                    pid_t pid = fork();
                    if (pid == 0) {
                        char num_cuenta_str[10];
                        char msqid_str[10];
                        char shm_id_str[10];
                        sprintf(num_cuenta_str, "%d", num_cuenta_autenticado);
                        sprintf(msqid_str, "%d", msqid);
                        sprintf(shm_id_str, "%d", shm_id);
                        execlp("gnome-terminal", "gnome-terminal", "--", "./usuario", num_cuenta_str, msqid_str, shm_id_str, NULL);
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
            }
            case 2: // REGISTRO USUARIO
                registrar_usuario();
                break;
            case 3: // SALIR DEL PROGRAMA
                printf("Saliendo...\n");
                pthread_mutex_lock(&tabla->mutex);
                FILE *archivo = fopen("data/cuentas.dat", "wb");
                if (archivo) {
                    fwrite(tabla->cuentas, sizeof(Cuenta), MAX_CUENTAS, archivo);
                    fwrite(&tabla->num_cuentas, sizeof(int), 1, archivo);
                    fclose(archivo);
                }
                pthread_mutex_unlock(&tabla->mutex);
                shmdt(tabla);
                shmctl(shm_id, IPC_RMID, NULL);
                sem_unlink("/cuentas_sem");
                sem_close(semaforo);
                close(pipefd[0]);
                close(pipefd[1]);
                msgctl(msqid, IPC_RMID, NULL);
                exit(0);
            default:
                printf("Opción no válida.\n");
                break;
        }
    }

    return 0;
}
