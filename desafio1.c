#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <getopt.h>

//volatile sig_atomic_t token_recibido = 0;
volatile sig_atomic_t proceso_siguiente_recibido = 0;
//int token_actual = -1;
pid_t proceso_siguiente = -1;
int max_decrecimiento = 0;
int numero_hijo = -1;

//Entradas:    Señal recibida (SIGUSR1), valor de la señal, informacion del proceso de la señal
//Salida:      No retorna nada
//Descripcion: Maneja la señal SIGUSR1, recibe el token y lo decrece en un valor aleatorio entre 0 y M 
//             y lo envia al siguiente proceso, si el token es menor a 0, el proceso se elimina.
//             Si el manejador es 2, significa que se recibe la señal de un proceso que ha muerto y se debe cambiar el siguiente proceso
//             Si el manejador es distinto e 1 y 2 significa que se recibe la señal de un proceso que ha ganado
void manejador_SIGUSR1(int sig, siginfo_t *si, void *context) {
    int valor_señal = si->si_value.sival_int;
    int manejador = (valor_señal)%10;
    if(manejador == 1){ // Manejador token

        int token = (valor_señal)/10;
        
        //calculo del decrecimiento
        int decrecimiento = rand() % max_decrecimiento;
        int nuevo_token = token - decrecimiento;

        printf("Proceso %d ; Token recibido: %d ; lo disminuye en %d ; Token resultante: %d\n", numero_hijo, token, decrecimiento, nuevo_token);

        if (nuevo_token < 0) {
            printf("(Proceso %d eliminado)\n", numero_hijo);
            exit(0);
        }

        if (proceso_siguiente > 0) {
            union sigval value;
            value.sival_int = (nuevo_token*10) + 1;
            if (sigqueue(proceso_siguiente, SIGUSR1, value) == -1) {
                perror("sigqueue");
            }
        }

        //token_actual = token;
        //token_recibido = 1;
    }
    else if(manejador == 2){ // Manejador notificacion cambio de proceso siguiente cuando muere un proceso
        proceso_siguiente_recibido++;
    }
    else{ // Manejador notificacion
        int procesos_restantes = (valor_señal)/10;
        if(procesos_restantes == 1){
            printf("Proceso %d es el ganador\n", numero_hijo);
            exit(0);
        }
    }
}

//Entradas:    Señal recibida (SIGUSR2), valor de la señal, informacion del proceso de la señal
//Salidas:     No retorna nada
//Descripcion: Maneja la señal SIGUSR2, actualiza la referencia del siguiente proceso
//             Tambien maneja el cambio de lider de procesos
void manejador_SIGUSR2(int sig, siginfo_t *si, void *context){
    int valor_señal = si->si_value.sival_int;
    int manejador = valor_señal%10;
    //printf("manejador = %d\n", manejador);
    if(manejador == 1){ // Manejador siguiente proceso
        pid_t pid_proceso_sig = (valor_señal)/10;
        // debug, comprobar si se forma el anillo
        // printf("el proceso siguiente del proceso %d es el %d\n", getpid(), pid_proceso_sig);
        proceso_siguiente = pid_proceso_sig;
        proceso_siguiente_recibido = 1;
    }
    else if(manejador == 2){
        pid_t pid_proceso_sig = (valor_señal)/10;
        proceso_siguiente = pid_proceso_sig;
        union sigval value;
        value.sival_int = 12;
        if (sigqueue(getppid(), SIGUSR1, value) == -1) {
            perror("sigqueue");
        }
    }
    else{ // Manejador notificacion lider
        int token_reiniciado = (valor_señal)/10;
        union sigval value;
        value.sival_int = (token_reiniciado*10) + 1;
        if (sigqueue(proceso_siguiente, SIGUSR1, value) == -1) {
            perror("sigqueue");
        }
    }
}

//Entradas:    Cantidad de hijos a crear, mascara de señales, numero de procesos
//Salidas:     Retorna un puntero a un arreglo de pids de los hijos creados
//Descripcion: Crea los procesos hijos, cada uno espera las señales para operar
pid_t* crear_hijos(int cantidad,  sigset_t *oldmask, int numero) {
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
            numero_hijo = i + 1;
            srand(getpid());
            while(proceso_siguiente_recibido == 0){
                sigsuspend(oldmask);
            }
            while(1) {
                sigsuspend(oldmask);
            }
        } else {
            // Padre guarda el PID
            pids[i] = pid;
        }
    }
    return pids;
}

//Entradas:    Pids de los hijos, cantidad de hijos, token inicial, mascara de señales, valor de la señal, valor de la señal 2
//Salidas:     No retorna nada
//Descripcion: Maneja el ciclo de vida de los hijos. Modifica los procesos y actualiza el arreglo de PIDs
void manejar_hijos(pid_t *pids, int *hijos, int token, sigset_t *oldmask, union sigval value, union sigval value2){
    while(*hijos > 1){
        pid_t pid_hijo_terminado = wait(NULL);
        // cuando uno de los procesos termina se desconecta del "anillo" de procesos
        for (int j = 0; j < *hijos; j++) {
            if(pids[j] == pid_hijo_terminado){
                if(j == (*hijos-1)){
                    value2.sival_int = (pids[0]*10) + 2;
                    if (sigqueue(pids[j-1], SIGUSR2, value2) == -1){
                        perror("sigqueue");
                    }
                }
                else if(j == 0){
                    value2.sival_int = (pids[j+1]*10) + 2;
                    if (sigqueue(pids[*hijos-1], SIGUSR2, value2) == -1){
                        perror("sigqueue");
                    }
                }
                else{
                    value2.sival_int = (pids[j+1]*10) + 2;
                    if (sigqueue(pids[j-1], SIGUSR2, value2) == -1){
                        perror("sigqueue");
                    }
                }
                break;
            }            
        }

        // el padre espera que efectivamente el hijo tenga el cambio del siguiente proceso
        sigsuspend(oldmask);

        // Se elimina el pid del proceso eliminado del arreglo de pids 
        pid_t *pids_mod = malloc((*hijos-1)*sizeof(pid_t));
        if (!pids_mod) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        int k = 0;
        for(int i = 0; i < *hijos; i++){
            if(pid_hijo_terminado != pids[i]){
                pids_mod[k] = pids[i];
                k++;
            }
        }
        //free(pids);
        pids = pids_mod;
        (*hijos)--;
        
        /* da problemas
        // Se notifica a los procesos que se elimino un proceso y se le envia la cantidad de procesos restantes
        value.sival_int = (hijos*10);
        for(int i = 0; i < hijos; i++){
            if(sigqueue(pids[i], SIGUSR1, value) == -1){
                perror("sigqueue");
            }
        }
            */

        // Por ahora se elije al proceso que esta al principio del arreglo como el lider para reiniciar el desafio
        /* value2.sival_int = (token*10) + 1;
        // Deberia se SIGUSR2 pero da problemas
        if(sigqueue(pids[0], SIGUSR1, value2) == -1){
            perror("sigqueue");
        } */
        // Si solo queda un proceso, notificamos al ganador

        // Se supone que por el enunciado debemos "avisarle" a todos los hijos que se murio alguno
        value.sival_int = (*hijos * 10);
        if (sigqueue(pids[0], SIGUSR1, value) == -1) {
           perror("sigqueue");
        }

        value2.sival_int = (token * 10);
        if (sigqueue(pids[0], SIGUSR2, value2) == -1) {
            perror("sigqueue");
        }
        //printf("restantes = %d\n", hijos);
    }
}

//Entradas:    Pids de los hijos, token inicial, valor de la señal
//Salidas:     No retorna nada
//Descripcion: Envia el token inicial al primer hijo
void enviar_token(pid_t *pids, int token, union sigval value){
    value.sival_int = (token*10) + 1;
    if (sigqueue(pids[0], SIGUSR1, value) == -1) {
        perror("sigqueue");
    }
}

//Entradas:    Pids de los hijos, cantidad de hijos, valor de la señal
//Salidas:     No retorna nada
//Descripcion: Conecta los hijos entre si, enviando la señal SIGUSR2
void conectar_hijos(pid_t *pids, int hijos, union sigval value){
    for (int i = 0; i < hijos; i++) {
        if(i == (hijos-1)){
            value.sival_int = (pids[0]*10) + 1;
            if (sigqueue(pids[i], SIGUSR2, value) == -1) {
                perror("sigqueue");
            }
        }
        else{
            value.sival_int = (pids[i+1]*10) + 1;
            if (sigqueue(pids[i], SIGUSR2, value) == -1) {
                perror("sigqueue");
            }
        }
        
    }
}


int main(int argc, char *argv[]) {
    int opt;
    int token = -1, numero = -1, hijos = -1;
    srand(time(NULL));

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
                fprintf(stderr, "Formato correcto: %s -t <num> -M <num> -p <num>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    //Validar que se entreguen todos los argumentos
    // Si alguno de los argumentos es -1, significa que no se ingresó
    // el argumento correspondiente

    if (token == -1 || numero == -1 || hijos == -1) {
        fprintf(stderr, "Faltan argumentos. Formato correcto: %s -t <num> -M <num> -p <num>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    //Validar tipo de argumentos
    if (token < 0) {
        fprintf(stderr, "El token debe ser un número positivo.\n");
        exit(EXIT_FAILURE);
    }
    if (numero <= 1) {
        fprintf(stderr, "El número maximo de decrecimiento debe ser mayor a 1.\n");
        exit(EXIT_FAILURE);
    }
    if (hijos <= 0) {
        fprintf(stderr, "El número de hijos debe ser mayor a 0.\n");
        exit(EXIT_FAILURE);
    }

    //printf("Valores ingresados: t = %d, M = %d, p = %d\n", token, numero, hijos);

    max_decrecimiento = numero;

    struct sigaction sa;
    sigset_t mask, oldmask;

    //Configuracion del manejador para SIGUSR1
    sa.sa_flags = SA_SIGINFO; //Permite aceder a la informacion extendida
    sa.sa_sigaction = manejador_SIGUSR1; //Asigna el manejadro definido
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    //Configuracion del manejador para SIGUSR2
    sa.sa_sigaction = manejador_SIGUSR2; //Asigna el manejadro definido
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
    pid_t *pids = crear_hijos(hijos, &oldmask, numero);

    union sigval value;
    union sigval value2;


    // Enviar token solo al primer proceso
    enviar_token(pids, token, value);

    /* union sigval value;
    value.sival_int = (token*10) + 1;
    if (sigqueue(pids[0], SIGUSR1, value) == -1) {
        perror("sigqueue");
    } */

    //El padre conecta los procesos
    conectar_hijos(pids, hijos, value2);

    /* for (int i = 0; i < hijos; i++) {
        if(i == (hijos-1)){
            value2.sival_int = (pids[0]*10) + 1;
            if (sigqueue(pids[i], SIGUSR2, value2) == -1) {
                perror("sigqueue");
            }
        }
        else{
            value2.sival_int = (pids[i+1]*10) + 1;
            if (sigqueue(pids[i], SIGUSR2, value2) == -1) {
                perror("sigqueue");
            }
        }
        
    } */


    manejar_hijos(pids, &hijos, token, &oldmask, value, value2);

    /* // Espera que terminen
    for (int i = 0; i < hijos; i++) {
        waitpid(pids[i], NULL, 0);
    }*/

    
    wait(NULL);
    free(pids);
    return 0;
}
