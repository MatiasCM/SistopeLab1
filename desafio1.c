#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

volatile sig_atomic_t token_recibido = 0;

/*
* Manejador de se~n ales que procesa la se~nal SIGUSR1 .
* Se utiliza SA_SIGINFO para acceder a la informaci  ́on adicional enviada con
la se~nal.
*/
void manejador(int sig, siginfo_t * si, void * context ){
    // Extrae el token enviado a trav  ́es del campo sival_int de la uni  ́onsigval .

    int token = si->si_value.sival_int;
    printf("Proceso %d recibi  ́o el token : %d\n", getpid (), token);
    token_recibido = 1; // Indica que la se~nal ha sido recibida .
}

int main (){
    struct sigaction sa;
    sigset_t mask, oldmask;

    // Configuraci  ́on del manejador para SIGUSR1 .
    sa.sa_flags = SA_SIGINFO; // Permite acceder a informaci  ́onextendida .

    sa.sa_sigaction = manejador; // Asigna el manejador definido

    sigemptyset(&sa.sa_mask);
    if(sigaction(SIGUSR1, &sa, NULL) == -1){
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // Bloquea SIGUSR1 para evitar su entrega prematura .
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    if(sigprocmask(SIG_BLOCK, &mask, &oldmask) < 0){
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if( pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }else if(pid == 0) {
        // Proceso hijo : espera la se~nal de manera segura usandosigsuspend .

        while(!token_recibido) {
            // sigsuspend reemplaza temporalmente la m ́a scaraactual por ’oldmask ’,

            // permitiendo la recepci  ́on de SIGUSR1 .
            sigsuspend(&oldmask);
        }
        exit(0);
    }else {
        // Proceso padre : prepara y env  ́ıa la se~nal con el token .
        union sigval value;
        value.sival_int = 42; // Asigna el token ( valor de ejemplo).

        if(sigqueue(pid, SIGUSR1, value) == -1) {
            perror("sigqueue");
            exit(EXIT_FAILURE);
        }
        wait(NULL); // Espera a que el proceso hijo termine .
    }
    return 0;
}