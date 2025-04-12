#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <getopt.h>

volatile sig_atomic_t token_recibido = 0;

volatile sig_atomic_t proceso_siguiente_recibido = 0;

int token_actual = -1;

pid_t proceso_siguiente = -1;

int num_random(int numero){
    // num random entre 0 y numero - 1
    int num_random = rand() % numero;
    return num_random;
}

void ruleta(int numero){
    // por ahora todos tienen el mismo token y se ejecutan "al mismo tiempo" por lo que el token resultante debe ser el mismo
    printf("; token resultante: %d \n", token_actual - num_random(10));
    return;
}

void manejador_token(int sig, siginfo_t *si, void *context) {
    int token = si->si_value.sival_int;     
    printf("Proceso %d ; recibió el token: %d ", getpid(), token);
    token_actual = token;
    token_recibido = 1;

}

void manejador_sig_proceso(int sig, siginfo_t *si, void *context){
    pid_t pid_proceso_sig = si->si_value.sival_int;
    // debug, comprobar si se forma el anillo
    // printf("el proceso siguiente del proceso %d es el %d\n", getpid(), pid_proceso_sig);
    proceso_siguiente_recibido = 1;
}

pid_t* crear_hijos(int cantidad, int token, sigset_t *oldmask, int numero) {
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
            while(proceso_siguiente_recibido == 0 || token_recibido == 0){
                sigsuspend(oldmask);
            }
            ruleta(numero);
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
    srand(time(NULL));

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
    sa.sa_sigaction = manejador_token; //Asigna el manejadro definido
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    //Configuracion del manejador para SIGUSR2
    sa.sa_sigaction = manejador_sig_proceso; //Asigna el manejadro definido
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR2, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // Bloqueamos ambas señales SIGUSR1 y SIGUSR2
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    if (sigprocmask(SIG_BLOCK, &mask, &oldmask) < 0) {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }


    //Llamamos a la función para crear los hijos
    pid_t *pids = crear_hijos(hijos, token, &oldmask, numero);

    //El padre les envía el token
    union sigval value;
    value.sival_int = token;
    for (int i = 0; i < hijos; i++) {
        if (sigqueue(pids[i], SIGUSR1, value) == -1) {
            perror("sigqueue");
        }
    }

    //El padre conecta los procesos
    union sigval value2;
    for (int i = 0; i < hijos; i++) {
        if(i == (hijos-1)){
            value2.sival_int = pids[0];
            if (sigqueue(pids[i], SIGUSR2, value2) == -1) {
                perror("sigqueue");
            }
        }
        else{
            value2.sival_int = pids[i+1];
            if (sigqueue(pids[i], SIGUSR2, value2) == -1) {
                perror("sigqueue");
            }
        }
        
    }

    // Espera que terminen
    for (int i = 0; i < hijos; i++) {
        waitpid(pids[i], NULL, 0);
    }

    free(pids);
    return 0;
}
