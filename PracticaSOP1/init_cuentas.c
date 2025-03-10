#include <stdio.h>
#include <stdlib.h>

struct Cuenta {
    int numero_cuenta;
    char titular[50];
    float saldo;
    int num_transacciones;
};

int main() {
    FILE *archivo = fopen("data/cuentas.dat", "wb");
    if (archivo == NULL) {
        perror("Error al crear cuentas.dat");
        exit(1);
    }

    struct Cuenta cuentas[] = {
        {1001, "John Doe", 5000.00, 0},
        {1002, "Jane Smith", 3000.00, 0}
    };
    int num_cuentas = sizeof(cuentas) / sizeof(struct Cuenta);

    fwrite(cuentas, sizeof(struct Cuenta), num_cuentas, archivo);
    fclose(archivo);
    printf("Archivo cuentas.dat creado con éxito.\n");
    return 0;
}
