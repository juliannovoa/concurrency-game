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
#include <wait.h>
#include <errno.h>

#define KO_MSG 1
#define OK_MSG 2
#define BUFFER_SIZE 20

struct msg {
	long type;
	pid_t child_pid;
};

// Implementation of the function that initializes the semaphore
int init_sem (int semid, int valor){
        if (semctl(semid, 0, SETVAL, valor)==-1){
                perror("Error initializing semaphore");
                return -1;
        }
        return 0;
}
 
// Implementation of the function wait of the semaphore
int wait_sem(int semid) {
	struct sembuf op[1] = {{.sem_num=0, .sem_op=-1, .sem_flg= SEM_UNDO}};
	while (semop(semid, op, 1) == -1){
		if (errno != EINTR) {
			perror("Error executing wait_sem");
			return -1;
		}
	}
	return 0;
}

// Implementation of the function signal of the semaphore
int signal_sem (int semid) {
      struct sembuf op[1] = {[0].sem_num=0, [0].sem_op=1, [0].sem_flg= SEM_UNDO};

        if (semop(semid, op, 1) == -1){
                perror("Error executing signal_sem");
                return -1;
        }
        return 0;
}

//This funtion initializes the IPC systems given a key
int init_IPC(key_t *key, int *queueid, int *shmid, int *semid, FILE **result, pid_t **children_pid, int init_proc_number,int (*barrera)[], char *filename, char *filepath){
	// Create a new key
	if((*key=ftok(filename,'X'))==-1){
		perror("Error creating the key");
		return -1;
	}
        // Get message queue for a given key
        if ((*queueid=msgget(*key, IPC_CREAT | 0600)) ==-1) {
                perror("Error creating the message queue");
                return -1;
        }
        // Get share memory ID for a given key
        if ((*shmid = shmget(*key, init_proc_number*sizeof(pid_t), IPC_CREAT | 0600)) == -1) {
		perror("Error creating shared memory area");
		return -1;
	}
        // Attach share memory to desired variable
        if ((*children_pid = (pid_t *) shmat(*shmid,0,0)) == ((void *) -1)) {
		perror("Error attaching sahred memory area");
		return -1;
	}
        // Get and initialize semaphore for a given key
        if ((*semid=semget(*key, 1, IPC_CREAT| 0600))==-1){
                perror("Error creating semaphore");
                return -1;
        }
	if(init_sem(*semid,1) == -1){
		perror("Error initializing the semaphore");
		return -1;
	}
	// Pipe creation
	if (pipe(*barrera) == -1){
		perror ("Error creating the pipe");
		return -1;
	}
	// Open FIFO file
	if ((*result = fopen(filepath,"w")) == NULL)  {
		perror("Error opening fifo file");
		return -1;
	}
        return 0;
}

//This funtion closes the IPC systems
void close_IPC(int queueid, int shmid, int semid, pid_t *children_pid, int barrera[2], FILE *result) {
	if(msgctl(queueid, IPC_RMID,NULL) != 0) {
		perror("Error closing message queue");
	}
	if(shmdt(children_pid) == -1) {
		perror("Error detaching shared memory area");
	}
	if(shmctl(shmid, IPC_RMID,0) == -1) {
		perror("Error removing shared memory");
	}
	if(semctl(semid, IPC_RMID,0) == -1) {
		perror("Error closing semaphore");
	}
	if(close(barrera[0]) == -1) {
		perror("Error closing pipe (reader)");
	}
	if(close(barrera[1]) == -1) {
		perror("Error closing pipe (writer)");
	}
	if(fclose(result) != 0) {
		perror("Error closing file FIFO");
	}
}

// Function that parses the main arguments.
int parse_arguments(int *init_proc_number, int argc, char *proc_numb_str){

	if ((argc) != 4){
		perror ("Illegal number of arguments.");
                return -1;
        } else if (sscanf (proc_numb_str, "%d", init_proc_number)  != 1){
		perror ("Some arguments do not match the specified numeric format.");
		return -1;            
	} else {
	return 0;
	}     
}

// This function creates the indicated number of children.
int create_children(int fd, int key, int init_proc_number, int semid, pid_t *children_pid, char *argv[]){
	pid_t child_pid;
	char key_str[BUFFER_SIZE], num_processes_str[BUFFER_SIZE];

	snprintf(key_str,BUFFER_SIZE,"%ld", (long) key);
	snprintf(num_processes_str,BUFFER_SIZE, "%d", init_proc_number);

	if (wait_sem(semid) != 0) {
		return -1;
	}
	for (int i = 0; i < init_proc_number; i++){
		if ((child_pid=fork()) == -1){
			printf("Error executing fork");
			exit(-1);
		} else if (child_pid == 0) {
			dup2(fd, STDIN_FILENO);
			execl(argv[2],"HIJO", key_str, num_processes_str, NULL);
		} else {
			children_pid[i] = child_pid;
		}
	}
	if (signal_sem(semid) != 0) {
		return -1;
	}
	return 0;
}

// This function write a given numberof bytes in a file
int syncronize_begining (int fd, int number_of_processes) {
	for (int i = 0; i < number_of_processes; i++){
		if (write(fd, "K" , 1) != 1){
			perror("Error writing in the pipe\n");
		}
	}
	return 0;
}

/* This fuction sends a SIGTERM signal to a process. If the flag value is 0, the function sends the signal to the
   specified process. If flag value is 1, the function sends the signal to the first process that is found       */
pid_t kill_process(pid_t child_pid, pid_t *children_pid, int semid, int init_processes, int flag) {
	pid_t killed_pid;

	if (wait_sem(semid) != 0) {
		return -1;
	}
	for (int i = 0; i < init_processes; i++){
		if ( ((children_pid[i] == child_pid) && (flag == 0)) || ((children_pid[i] != 0) && (flag == 1)) ){
			if (kill(children_pid[i], SIGTERM) != 0) {
				perror("Error sending SIGTERM");
				return -1;
			}
			if (wait(NULL) != children_pid[i]) {
				perror("Error finishing child process");
				return -1;
			}
			killed_pid = children_pid[i];
			children_pid[i]=0;
			break;
		}
	}
	if (signal_sem(semid) != 0) {
		return -1;
	}
	return killed_pid;
}

/* This function receives messages from the children and if one of them has been defeated it is killed
   On succes, returns the number of killes processes, on error, -1 is returned                         */
int receive_result(int semid, int queueid, int current_processes, int init_processes, pid_t *children_pid){
	struct msg message;
	int length = sizeof(message.child_pid);
	int killed_processes = 0;

	for (int i = 0; i < current_processes; i++){
		if(msgrcv(queueid, &message, length, 0, 0)==-1){
			perror("Error receiving message");
			return -1;
		}
		if (message.type == KO_MSG){
			if (kill_process(message.child_pid, children_pid, semid, init_processes, 0) == -1){
				return -1;
			}
			killed_processes++;
		}
	}
	return killed_processes;
}

// This function prints the result of the battle
int print_result(pid_t *children_pid, int semid, int current_processes, int init_proc_number, FILE *resultado){
	pid_t winner;
	
	if (current_processes == 0){
		fprintf(resultado,"End of the game: Tie\n");
	} else {
		if((winner = kill_process(0, children_pid, semid, init_proc_number, 1)) == -1) {
			return -1;
		}
		fprintf(resultado,"End of the game: Child %ld has won.\n", (long) winner);
	}
	return 0;
}

// This funtion controls the battle
int start_game (int init_proc_number, int fd, int queueid, pid_t *children_pid, int semid, FILE *resultado) { 
	int current_processes = init_proc_number;
	int killed_processes;
	int round = 1;
	
	while (current_processes >= 2){
		printf("Initiating attack round number %d. There are %d children.\n", round,current_processes);
		fflush(stdout);
		// Start the round
		if(syncronize_begining(fd, current_processes) != 0) {
			return -1;
		}
		// Receive the result of the round
		if ((killed_processes = receive_result(semid,queueid,current_processes,init_proc_number,children_pid)) == -1){
			return -1;
		}			
		current_processes -= killed_processes;
		round++;
		printf("\n\n\n");
		fflush(stdout);
	}
	// Print the result of the battle
	if (print_result(children_pid, semid, current_processes, init_proc_number, resultado) != 0) {
		return -1;
	}
	return 0;
}
		
int main(int argc, char *argv[]){
	key_t key;
        int queueid, shmid, semid;
        pid_t *children_pid;
        int init_proc_number;	
	int barrera[2];
	FILE *result;
	
	// Parsing arguments
	if (parse_arguments( &init_proc_number, argc, argv[1]) != 0) {
		return -1;
	}

	// Initialize InterProcess Comunication systems.	
	if (init_IPC(&key, &queueid, &shmid, &semid, &result, &children_pid, init_proc_number, &barrera, argv[0], argv[3]) != 0) {
                return -1;
        }

	// Create children processes
	if(create_children(barrera[0], key, init_proc_number, semid, children_pid, argv) != 0) {
		return -1;
	}

	// Begin the battle
	if(start_game(init_proc_number, barrera[1], queueid, children_pid, semid, result) != 0){
		return -1;
	}

	// Close InterProcess Comunication systems.	
	close_IPC(queueid, shmid, semid, children_pid, barrera, result);

	// Demonstration that IPC systems are closed
	system("ipcs -qs --human");
}

