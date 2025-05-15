#ifndef CUENTA_H
#define CUENTA_H

struct Cuenta {
    int numero_cuenta;
    char titular[50];
    float saldo;
    int num_transacciones;
};

#endif // CUENTA_H
