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

// Entradas:    Señal recibida (sig) en este caso debe recibir SIGUSR1, (*si) puntero a la estructura que contiene 
//              la informacion de la señal (en este caso se usa para enviar valores a traves de las señales) y (*context) 
//              informacion del proceso de la señal (en este caso no se usa)
//
// Salida:      No retorna nada
//
// Descripcion: Maneja la señal SIGUSR1, recibe un valor de la señal a traves del campo si_value de si, este valor es 
//              interpretado como un entero con sival_int, este debe tener la forma (valor + manejador) de modo que el 
//              numero menos significativo es el "manejador" que se usara y los demas es el valor que se usara 
//              (ej: 9872, el manejador es el 2 y el valor es 987).
//
//              Si el manejador es 1, el valor que recibe es el token y lo decrece en un valor aleatorio entre 0 y M 
//              y lo envia al siguiente proceso, si el token es menor a 0, el proceso se elimina.
// 
//              Si el manejador es 2, significa que la señal recibida es de un proceso que cambio su proceso siguiente,
//              esta señal la recibe el proceso padre como aviso para poder continuar su ejecucion luego de que le dijo a 
//              uno de sus hijos que cambiara su proceso siguiente luego de que alguno de los procesos hijos finalizara.
//              O tambien significa que el hijo confirma que recibio la señal de que un proceso termino.
//
//              Si el manejador es distinto e 1 y 2 significa que el valor recibido es el numero de procesos aun activos
//              "jugando", si el valor recibido es 1, el proceso se declara el ganador y termina su ejecucion, si no, avisa
//              al padre que recibio la señal.

void manejador_SIGUSR1(int sig, siginfo_t *si, void *context){
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
    }
    else if(manejador == 2){ // Manejador notificacion padre
        // Simplemente una notificacion al padre de que los hijos recibieron la señal para cambiar el proceso siguiente
        // o el aviso de que un proceso termino
    }
    else{ // Manejador notificacion
        int procesos_restantes = (valor_señal)/10;
        if(procesos_restantes == 1){
            printf("Proceso %d es el ganador\n", numero_hijo);
            exit(0);
        }
        else{
            union sigval value1;
            value1.sival_int = 2;
            if (sigqueue(getppid(), SIGUSR1, value1) == -1) {
                perror("sigqueue");
            }
        }
    }
}

// Entradas:    Señal recibida (sig) en este caso debe recibir SIGUSR2, (*si) puntero a la estructura que contiene 
//              la informacion de la señal (en este caso se usa para enviar valores a traves de las señales) y (*context) 
//              informacion del proceso de la señal (en este caso no se usa)
//
// Salidas:     No retorna nada
//
// Descripcion: Maneja la señal SIGUSR2, recibe un valor de la señal a traves del campo si_value de si, este valor es 
//              interpretado como un entero con sival_int, este debe tener la forma (valor + manejador) de modo que el 
//              numero menos significativo es el "manejador" que se usara y los demas es el valor que se usara.
//
//              Si el manejador es 1, el valor que recibe es el pid del proceso siguiente a el, el cual guarda en 
//              proceso_siguiente.
//
//              Si el manejador es 2, el valor recibido es un nuevo proceso siguiente el cual debe guardar en
//              proceso_siguiente y luego avisarle al padre que lo recibio.
//
//              Si el manejador es distinto de 1 y 2, significa que la señal recibida es del padre asignandolo como
//              lider y el valor recibido es para que reinicie el token y continue el desafio.

void manejador_SIGUSR2(int sig, siginfo_t *si, void *context){
    int valor_señal = si->si_value.sival_int;
    int manejador = valor_señal%10;
    
    if(manejador == 1){ // Manejador siguiente proceso
        pid_t pid_proceso_sig = (valor_señal)/10;
        proceso_siguiente = pid_proceso_sig;
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

// Entradas:    Cantidad de hijos a crear (cantidad), puntero a una mascara de señales (*oldmask) (debe ser una 
//              mascara con las señales desbloqueadas) y numero de procesos (numero).
//
// Salidas:     Retorna un puntero a un arreglo de pids de los hijos creados.
//
// Descripcion: Crea los procesos hijos con fork(), cada uno se queda en un infinito ciclo esperando señales para operar.

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
            srand(time(NULL));
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

// Entradas:    Puntero al arreglo con los pids de los hijos (*pids), puntero al entero con la cantidad de hijos (*hijos), 
//              token inicial (token), puntero a una mascara de señales (*oldmask) (con las señales desbloqueadas).
//
// Salidas:     No retorna nada
//
// Descripcion: Maneja el ciclo de vida de los hijos, el padre espera a que alguno de sus hijos termine, cuando uno termina
//              este es desconectado del anillo de procesos y se le avisa al proceso anterior a el que debe cambiar su proceso
//              siguiente, el padre espera a que este cambio sea efectivo y continua eliminando el pid del hijo terminado del
//              arreglo de pids, luego le avisa a todos los procesos que termino hijo y les envia el numero de procesos restantes
//              en el desafio, y asigna al proceso con el pid mas bajo que sea el lider y le envia la señal con el token para
//              que continue el desafio reiniciando el token.

void manejar_hijos(pid_t *pids, int *hijos, int token, sigset_t *oldmask){

    // Valor para la señal SIGUSR1
    union sigval value;
    // Valor para la señal SIGUSR2
    union sigval value2;

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
        pids = NULL;
        pids = pids_mod;
        (*hijos)--;

        // si hay mas de 1 hijo, se avisa a todos que uno termino y se continua el desafio asignando un lider
        if(*hijos > 1){
            // Se supone que por el enunciado debemos "avisar" a todos los hijos que se termino alguno
            for(int i = 0; i < *hijos; i++){
                value.sival_int = (*hijos * 10);
                if (sigqueue(pids[i], SIGUSR1, value) == -1) {
                perror("sigqueue");
                }
                // el padre espera la confirmacion de que le llego la señal al hijo
                sigsuspend(oldmask);
            }
            // Mecanismo para asignar el lider (quien tiene el pid mas bajo)
            int hijo_pid_mas_bajo = *hijos - 1;
            for(int i = 0; i < *hijos; i++){
                if(pids[hijo_pid_mas_bajo] > pids[i]){
                    hijo_pid_mas_bajo = i;
                }
            }

            // Se le envia la señal al lider para que continue con el desafio

            value2.sival_int = (token * 10);
            if (sigqueue(pids[hijo_pid_mas_bajo], SIGUSR2, value2) == -1) {
                perror("sigqueue");
            }
        }
        // si hay menos de 2 hijos, se avisa al proceso que queda que termino uno
        else{
            value.sival_int = (*hijos * 10);
            if (sigqueue(pids[0], SIGUSR1, value) == -1) {
            perror("sigqueue");
            }
        }
    }
}

// Entradas:    Puntero al arreglo con los pids de los hijos (*pids), token inicial (token)
//
// Salidas:     No retorna nada
//
// Descripcion: Envia el token inicial al primer hijo

void enviar_token(pid_t *pids, int token){
    union sigval value;
    value.sival_int = (token*10) + 1;
    if (sigqueue(pids[0], SIGUSR1, value) == -1) {
        perror("sigqueue");
    }
}

// Entradas:    Puntero al arreglo con los pids de los hijos (*pids), cantidad de hijos (hijos)
//
// Salidas:     No retorna nada
// 
// Descripcion: Conecta los hijos entre si, enviando la señal SIGUSR2

void conectar_hijos(pid_t *pids, int hijos){
    union sigval value2;
    for (int i = 0; i < hijos; i++) {
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

    // Validar que se entreguen todos los argumentos
    // Si alguno de los argumentos es -1, significa que no se ingresó
    // el argumento correspondiente

    if (token == -1 || numero == -1 || hijos == -1) {
        fprintf(stderr, "Faltan argumentos. Formato correcto: %s -t <num> -M <num> -p <num>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Validar tipo de argumentos
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

    max_decrecimiento = numero;

    struct sigaction sa;
    sigset_t mask, oldmask;

    // Configuracion del manejador para SIGUSR1
    sa.sa_flags = SA_SIGINFO; //Permite acceder a la informacion extendida
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

    // Enviar token solo al primer proceso
    enviar_token(pids, token);


    // El padre conecta los procesos
    conectar_hijos(pids, hijos);


    manejar_hijos(pids, &hijos, token, &oldmask);

    // Luego de que deja de manejar a los hijos, el padre espera a que el proceso ganador termine
    wait(NULL);
    free(pids);
    return 0;
}
