#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define MAX_LINE 1024

void *detectar_fraude(void *arg) {
    char buffer[MAX_LINE];
    while (read(0, buffer, MAX_LINE) > 0) { // Leer de la tubería (stdin)
        printf("Monitor detectó: %s\n", buffer);
        // Aquí implementar lógica de detección de fraude en tiempo real
    }
    return NULL;
}

int main() {
    pthread_t hilo;
    pthread_create(&hilo, NULL, detectar_fraude, NULL);
    pthread_join(hilo, NULL);
    return 0;
}
