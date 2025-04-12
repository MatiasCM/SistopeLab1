#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <getopt.h>

volatile sig_atomic_t token_recibido = 0;

/*
* Manejador de señales que procesa la señal SIGUSR1.
*/
void manejador(int sig, siginfo_t *si, void *context) {
    int token = si->si_value.sival_int;
    printf("Proceso %d recibió el token: %d\n", getpid(), token);
    token_recibido = 1;
}

int main(int argc, char *argv[]) {
    int opt;
    int token = -1, numero = -1, hijos = -1;

    // Procesamiento de argumentos con getopt
    while ((opt = getopt(argc, argv, "t:M:p:")) != -1) {
        switch (opt) {
            case 't':
                token = atoi(optarg);
                break;
            case 'M':
                numero = atoi(optarg);
                break;
            case 'p':
                hijos = atoi(optarg);
                break;
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

    // Configuración del manejador de señales
    struct sigaction sa;
    sigset_t mask, oldmask;

    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = manejador;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &mask, &oldmask) < 0) {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }

    // Arreglo para almacenar los PIDs de los hijos
    pid_t *pids = malloc(hijos * sizeof(pid_t));
    if (pids == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    // Crear procesos hijos
    for (int i = 0; i < hijos; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (pid == 0) {
            // Proceso hijo
            while (!token_recibido) {
                sigsuspend(&oldmask);
            }
            exit(0);
        } else {
            // Proceso padre guarda el PID
            pids[i] = pid;
        }
    }

    // Padre envía el token a cada hijo
    union sigval value;
    value.sival_int = token;
    for (int i = 0; i < hijos; i++) {
        if (sigqueue(pids[i], SIGUSR1, value) == -1) {
            perror("sigqueue");
        }
    }

    // Esperar que todos los hijos terminen
    for (int i = 0; i < hijos; i++) {
        waitpid(pids[i], NULL, 0);
    }

    free(pids);
    return 0;
}
