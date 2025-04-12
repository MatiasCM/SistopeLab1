#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <getopt.h>

volatile sig_atomic_t token_recibido = 0;

void manejador(int sig, siginfo_t *si, void *context) {
    int token = si->si_value.sival_int;
    printf("Proceso %d recibió el token: %d\n", getpid(), token);
    token_recibido = 1;
}

pid_t* crear_hijos(int cantidad, int token, sigset_t *oldmask) {
    pid_t *pids = malloc(cantidad * sizeof(pid_t));
    if (!pids) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < cantidad; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (pid == 0) {
            // Hijo
            while (!token_recibido) {
                sigsuspend(oldmask);
            }
            exit(0);
        } else {
            // Padre guarda el PID
            pids[i] = pid;
        }
    }
    return pids;
}

int main(int argc, char *argv[]) {
    int opt;
    int token = -1, numero = -1, hijos = -1;

    while ((opt = getopt(argc, argv, "t:M:p:")) != -1) {
        switch (opt) {
            case 't': token = atoi(optarg); break;
            case 'M': numero = atoi(optarg); break;
            case 'p': hijos = atoi(optarg); break;
            default:
                fprintf(stderr, "Uso: %s -t <num> -M <num> -p <num>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (token == -1 || numero == -1 || hijos == -1) {
        fprintf(stderr, "Faltan argumentos. Uso: %s -t <num> -M <num> -p <num>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    printf("Valores ingresados: t = %d, M = %d, p = %d\n", token, numero, hijos);

    struct sigaction sa;
    sigset_t mask, oldmask;

    //Configuracion del manejador para SIGUSR1
    sa.sa_flags = SA_SIGINFO; //Permite aceder a la informacion extendida
    sa.sa_sigaction = manejador; //Asigna el manejadro definido
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    //Bloquea SIGUSR1 para evitar su entrega prematura
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &mask, &oldmask) < 0) {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }

    //Llamamos a la función para crear los hijos
    pid_t *pids = crear_hijos(hijos, token, &oldmask);

    //El padre les envía el token
    union sigval value;
    value.sival_int = token;
    for (int i = 0; i < hijos; i++) {
        if (sigqueue(pids[i], SIGUSR1, value) == -1) {
            perror("sigqueue");
        }
    }

    // Espera que terminen
    for (int i = 0; i < hijos; i++) {
        waitpid(pids[i], NULL, 0);
    }

    free(pids);
    return 0;
}
