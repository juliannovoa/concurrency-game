#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>

#define STATE_LENGTH 3
#define KO_MSG 1
#define OK_MSG 2
#define SHORT_WAIT 100000000L
#define LONG_WAIT 200000000L
#define SUCCESS_STATE "OK"
#define FAILURE_STATE "KO"

struct msg {
	long type;
	pid_t child_pid;
};

// Global variables
char state[STATE_LENGTH];
pid_t mypid;

// Implementation of the function that initializes the semaphore
int init_sem(int semid, int valor) {
	if (semctl(semid, 0, SETVAL, valor) == -1){
		perror("Error al inicializar el semaforo");
		return -1;
	}
	return 0;
}

// Implementation of the function wait of the semaphore
int wait_sem(int semid) {
	struct sembuf op[1] = {{.sem_num=0, .sem_op=-1, .sem_flg= SEM_UNDO}};
	while (semop(semid, op, 1) == -1){
		if (errno != EINTR) {
			perror("Error en la operacion wait_sem");
			return -1;
		}
	}
	return 0;
}

// Implementation of the function signal of the semaphore
int signal_sem(int semid) {
	struct sembuf op[1] = {{.sem_num=0, .sem_op=1, .sem_flg= SEM_UNDO}};
	if (semop(semid, op, 1) == -1){
		perror("Error en la operacion signal_sem");
		return -1;
	}
	return 0;
}

// Handler for SIGUSR1 signal. Defence mode.
void defensa(int sig, siginfo_t *info, void *context) {
	printf ("El hijo %ld ha repelido el ataque de %ld.\n", (long) mypid, (long) info->si_pid);
	fflush(stdout);
	strcpy(state, SUCCESS_STATE);
}

// Handler for SIGUSR1 signal. Attack mode.
void indefenso(int sig, siginfo_t *info, void *context) {
	printf("El hijo %ld ha sido emboscado por %ld mientras realizaba un ataque.\n", (long) mypid, (long) info->si_pid);
	fflush(stdout);
	strcpy(state, FAILURE_STATE);
}

//This funtion initializes the IPC systems given a key
int init_IPC(key_t key, int *queueid, int *semid, pid_t **children_pid, int init_proc_number) {
	int shmid;

	// Get message queue for a given key
	if ((*queueid=msgget(key, IPC_CREAT | 0600))==-1) {
		perror("Error al crear la cola de mensages");
		return -1;
	}
	
	// Get share memory ID for a given key
	if ((shmid = shmget(key, init_proc_number*sizeof(pid_t), IPC_CREAT | 0600)) == -1) {
		perror("Error al crear el area de memoria compartida hijo");
		return -1;
	 }

	// Attach share memory to desired variable
	if ((*children_pid = (pid_t *) shmat(shmid,0,0)) == ((void *) -1)) {
		perror("Error al asociar memoria compartida");
		return -1;
	}

	// Get semaphore for a given key
	if ((*semid=semget(key, 1, IPC_CREAT| 0600)) == -1) {
		perror("Error al crear el semaforo");
		return -1;
	}
	return 0;
}

// This function implementes a "sleep" that contuniues even if it is interrupted by a signal
int mysleep(long ntime) {
	struct timespec req, rem;
	req.tv_sec = 0;
	req.tv_nsec = ntime;
	
	while (nanosleep(&req, &rem) == -1) {
		if (errno == EINTR) {
			req.tv_nsec = rem.tv_nsec;
		} else {
			perror("Error en el sleep");
			return -1;
		}
	}
	return 0;
}

// This function contains the defence mode actions
int defence( struct sigaction *act) {
	act->sa_sigaction = defensa;	
	if (sigaction(SIGUSR1, act, NULL) != 0) {
		perror("Error al asignar manejador de señal");
		return -1;
	}
	printf("El hijo %ld decide defenderse.\n", (long) mypid);
	fflush(stdout);
	if(mysleep(LONG_WAIT) != 0 ){
		return -1;
	}
	return 0;
}

// This function contains the attack mode actions
int attack(int semid, int init_proc_number, pid_t *children_pid, struct sigaction *act) {
	int attackto;	
	act->sa_sigaction = indefenso;
	// Begin the defence, selecting "defensa" handler.
	if (sigaction(SIGUSR1, act, NULL) != 0){
		perror("Error al asignar manejador de señal");
		return -1;
	}
	printf("El hijo %ld decide atacar.\n", (long) mypid);
	fflush(stdout);
	if(mysleep(SHORT_WAIT) != 0 ){
		return -1;
	}
	// Find a suitable process to attack
	if (wait_sem(semid) != 0) {
		return -1;
	}
	do {
		attackto = children_pid[rand() % init_proc_number];
	} while (attackto == 0 || attackto == mypid);
	if (signal_sem(semid) != 0) {
		return -1;
	}
	printf("El hijo %ld ataca a %d.\n", (long) mypid, attackto);
	fflush(stdout);
	// Send signal no the attacked process			
	if (kill (attackto, SIGUSR1) != 0) {
		perror("Error al mandar la señal");				
		return -1;
	}			
	if(mysleep(SHORT_WAIT) != 0 ){
		return -1;
	}
	return 0;
}

// Procedure that implements the behaviour of the children 
void start_children_game(int queueid, int semid, pid_t *children_pid, int init_proc_number) {
	char control[2];
	struct msg message = {.child_pid = mypid};
	size_t message_length = sizeof(message.child_pid);
	struct sigaction act ={.sa_flags = SA_SIGINFO};

	while (1) {
		// Read a byte from the pipe (redirected to stdin) to synchronize processes
		if (read(STDIN_FILENO, control, 1) != 1) {
			perror (" Error al leer de la tuberia");
			exit(-1);
		}
		strcpy(state,"");
		if ((rand() % 2) == 0) {
			// Select defence mode.		
			if (defence(&act) != 0) {
				exit(-1);
			}
		} else {
			// Select attack mode.			
			if (attack(semid, init_proc_number, children_pid, &act) != 0) {
				exit(-1);
			}
		}
		// Send message to the father, indicating the outcome of the battle
		message.type = strcmp(state, FAILURE_STATE) == 0 ? KO_MSG : OK_MSG;
		if (msgsnd(queueid, &message, message_length, 0) == -1) {
			perror("Error al enviar el mensaje");
			exit (-1);
		}
	}
}

// Function that parses the main arguments.
int parse_arguments(key_t *key, int *init_proc_number, int argc, char *argv[]){
	long temp_key;

	if (argc != 3){
		perror ("El número de argumentos es erróneo.");
		return -1;		
	} else if (sscanf (argv[1], "%ld", &temp_key) != 1|| sscanf (argv[2], "%d", init_proc_number) != 1){
		perror ("Alguno de los argumentos no tiene formato numérico esperado.");
		return -1;		
	} else {
		*key = (key_t) temp_key;
		return 0;
	}	
}

int main(int argc, char *argv[]){
	key_t key;
	int queueid, semid;
	pid_t *children_pid;
	int init_proc_number;
	
	// Initialize variables.
	mypid = getpid();
	if (parse_arguments(&key, &init_proc_number, argc, argv) == -1){
		return -1;
	}

	// Initialize InterProcess Comunication systems.	
	if (init_IPC(key, &queueid, &semid, &children_pid, init_proc_number) == -1) {
		return -1;
	}  

	// Initialize the pseudo-random number generator.
	srand(time(NULL) + (long) mypid);

	// Child starts the game.	
	start_children_game(queueid, semid, children_pid, init_proc_number);
}

