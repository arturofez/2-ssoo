/**
UNIX Shell Project
Author: Arturo Fernandez Perez

Sistemas Operativos
Grados I. Informatica, Computadores & Software
Dept. Arquitectura de Computadores - UMA

Some code adapted from "Fundamentos de Sistemas Operativos", Silberschatz et al.

To compile and run the program:
   $ gcc Shell_project_amp.c job_control.c -o Shellamp -pthread
   $ ./Shellamp
	(then type ^D to exit program)

**/

#include "job_control.h"   // remember to compile with module job_control.c 
#include <string.h>

#define MAX_LINE 256 /* 256 chars per line, per command, should be enough. */

#define BOLD	"\033[1m"
#define REGULAR	"\033[0m"
#define ROJO	"\x1b[31;1;1m"
#define NEGRO	"\x1b[0m"

job * joblist; // lista de tareas, global para usarla en el manejador

// variables globales para comando time-out
int timeo;
int pid_timeo;

// ---------------------------------------------------------------------
//                             MANEJADOR
// ---------------------------------------------------------------------

	void manejador(int senal)
	{
		int pid, status, info;
		enum status status_res;
		job * jobs = joblist->next;
		
		block_SIGCHLD(); // se bloquea la señal SIGCHLD cuando se accede a la lista de procesos
		while (jobs != NULL){
			pid = waitpid(jobs->pgid, &status, WUNTRACED | WNOHANG | WCONTINUED);
			status_res = analyze_status(status, &info);
			if (pid == jobs->pgid) {
				if (status_res == SUSPENDED) { // si se suspende
					printf(BOLD"\nBackground job suspended... pid: %d, command: %s, info: %d"REGULAR"\nCOMMAND->", 
						pid, jobs->command, info);
					fflush(stdout);
					jobs->state = STOPPED;
					jobs = jobs->next;
				} else if (status_res == CONTINUED) { // si se despierta
					printf(BOLD"\nBackground job running... pid: %d, command: %s, info: %d"REGULAR"\nCOMMAND->",
						pid, jobs->command, info);
					fflush(stdout);
					jobs->state = BACKGROUND;
					jobs = jobs->next;
				} else if(jobs->state == RESPAWNABLE) { // si proceso es respawnable
					int pidres = fork();
					if (pidres == 0) { // hijo resucitado
						new_process_group(getpid());
						restore_terminal_signals();
						execvp(jobs->command, jobs->args); 
						printf("Error resucitando '%s' \n", jobs->command);
						exit(-1);
					} else { // shell guarda el proceso resucitado
						printf(BOLD"\nRespawnable job exited... pid: %d, command: %s, info: %d\n"REGULAR,
							pid, jobs->command, info);
						fflush(stdout);
						job *aux = new_job(pidres, jobs->command, jobs->args, RESPAWNABLE);
						block_SIGCHLD();     
						add_job(joblist, aux);
						unblock_SIGCHLD();
						printf(BOLD"Command %s reborn...\n"REGULAR"COMMAND->", jobs->command);
						fflush(stdout);
						delete_job(joblist, jobs);
					}
				} else { // si finaliza
					printf(BOLD"\nBackground job exited... pid: %d, command: %s, info: %d"REGULAR"\nCOMMAND->", 
						pid, jobs->command, info);
					fflush(stdout);
					job* aux = jobs->next; 
					delete_job(joblist, jobs);
					jobs = aux;
				}
			} else {
				jobs = jobs->next;
			}
		}
		unblock_SIGCHLD(); // se desbloquea la señal SIGCHLD
	}
	
// -------------------------- Manejador para comando mask -------------
	void intercepcion(int senal)
	{
		printf(BOLD"\nEl proceso enmascarado ha interceptado la señal %s"REGULAR"\nCOMMAND->", 
			strsignal(senal));
		fflush(stdout);
	}
	
// -------------------------- Thread para comando time-out -------------

	void *alarm_thread(void *null) {
		int my_pid_timeo = pid_timeo;
		debug(my_pid_timeo, "%d");
		sleep(timeo);
		printf("Se acabó el tiempo! KILL!\n");
		kill(my_pid_timeo, SIGKILL);
		pthread_exit(NULL);
	}

// ---------------------------------------------------------------------
//                            MAIN
// ---------------------------------------------------------------------

int main(void)
{
	char inputBuffer[MAX_LINE]; /* buffer to hold the command entered */
	int background;             /* equals 1 if a command is followed by '&' */
	char *args[MAX_LINE/2];     /* command line (of 256) has max of 128 arguments */
	// probably useful variables:
	int pid_fork, pid_wait; /* pid for created and waited process */
	int status;             /* status returned by wait */
	enum status status_res; /* status processed by analyze_status() */
	int info;				/* info processed by analyze_status() */
	
	// variables para estado respawnable
	int respawnable; /* igual a 1 si el comando termina en '+' */
	
	// variables para comando mask
	int maskable; /* igual a 1 si comando mask */
	int sigs[20];
	sigset_t block_masksig;
	
	//variables para comando time-out
	int timeoutable;
	pthread_t tid_timeout;
	
	//inicializo la lista de procesos guardados
	args[0] = NULL;
	joblist = new_list("Job list", args);
	
	ignore_terminal_signals();
	signal(SIGCHLD, manejador); // capturar señal SIGCHLD y manejarla
	
	
	while (1)   /* Program terminates normally inside get_command() after ^D is typed*/
	{
		printf("COMMAND->");
		fflush(stdout);
		get_command(inputBuffer, MAX_LINE, args, &background, &respawnable);  /* get next command */
		
		if(args[0]==NULL) continue;   // if empty command
		
		//Comando internos cd	
		if(strcmp(inputBuffer, "cd") == 0) {	
			if (args[1] != NULL) { 	
				if (chdir(args[1]) == -1) { // cambia al directorio especificado	
					printf(ROJO"\nRuta '%s' no encontrada\n"NEGRO, args[1]);	
				}	
			} else {	
				chdir(getenv("HOME")); 	
			}	
			continue;	
		}
		
		//Comando interno jobs
		if(strcmp(inputBuffer, "jobs") == 0) {
			if (joblist->next == NULL) {
				printf("No hay tareas suspendidas o en background\n");
			} else {
				print_list(joblist, print_item);
			}
			continue;
		}
		
		//Comando interno fg
		if(strcmp(inputBuffer, "fg") == 0) {
			int n;
			
			
			if(args[1] == NULL) { // si no se indica, reanudamos el primero
				n = 1;
			} else {
				n = atoi(args[1]);
			}
			
			job * aux = get_item_bypos(joblist, n);
			
			if (aux == NULL) {
				printf("No hay tareas suspendidas o en background\n");
			} else {
				if (aux->state == STOPPED){
					printf(BOLD"Suspended job %d to foreground... pid: %d, command: %s"REGULAR"\n",
						n, aux->pgid, aux->command);
				} else if (aux->state == RESPAWNABLE) {
					printf(BOLD"Respawnable job %d to foreground... pid: %d, command: %s"REGULAR"\n",
						n, aux->pgid, aux->command);
				} else {
					printf(BOLD"Background job %d to foreground... pid: %d, command: %s"REGULAR"\n",
						n, aux->pgid, aux->command);
				}
				
				int pid = aux->pgid;
				int state = aux->state;
				char *com = strdup(aux->command);
				
				set_terminal(pid); // asignamos el terminal al proceso
				
				block_SIGCHLD();     
				delete_job(joblist, aux); // borramos el proceso de la lista
				unblock_SIGCHLD();     
				
				if (state == STOPPED){ // si estaba suspendido
					killpg(pid, SIGCONT); // envía una señal para que despierte
				}
				waitpid(pid, &status, WUNTRACED);
				
				set_terminal(getpgid(getpid())); //recupera el terminal
				status_res = analyze_status(status, &info);

				if (status_res == SUSPENDED) {
					job *aux = new_job(pid, com, args, STOPPED);
					block_SIGCHLD();
					add_job(joblist, aux);
					unblock_SIGCHLD();
					printf(BOLD"Foreground job suspended... pid: %d, command: %s, %s, info: %d\n"REGULAR, 
						pid, com, status_strings[status_res], info);
				} else {
				printf(BOLD"Foreground job exited... pid: %d, command: %s, %s, info: %d\n"REGULAR, 
						pid, com, status_strings[status_res], info);
				}
			}
			continue;
		}
		
		//Comando interno bg
		if(strcmp(inputBuffer, "bg") == 0) {
			int n;
			if(args[1] == NULL) { // si no se indica, reanudamos el primero
				n = 1;
			} else {
				n = atoi(args[1]);
			}
			
			job * aux = get_item_bypos(joblist, n);
			
			if (aux == NULL) {
				printf("No hay tareas suspendidas o en background\n");
			} else if (aux->state == BACKGROUND) {
				printf(BOLD"El proceso %d no está suspendido... pid: %d, command: %s"REGULAR"\n",
					n, aux->pgid, aux->command);
			} else if(aux->state == RESPAWNABLE) {
				aux->state = BACKGROUND;
				killpg(aux->pgid,SIGCONT);
				printf(BOLD"Respawnable job %d to background... pid: %d, command: %s"REGULAR"\n",
						n, aux->pgid, aux->command);
			} else {
				aux->state = BACKGROUND;
				killpg(aux->pgid,SIGCONT);
				printf(BOLD"Suspended job %d resumed... pid: %d, command: %s"REGULAR"\n",
						n, aux->pgid, aux->command);
			}
			continue;
		}
		
		//Comando interno mask
		if(strcmp(inputBuffer, "mask") == 0) {
			if (args[1] == NULL || args[2] == NULL || args[3] == NULL) {
				printf(ROJO"Error de sintaxis. Uso: mask <s> -c <cmd [with arguments]>"NEGRO"\n");
				continue;
			} 
			int i = 0, noer = 1, j = 0;
			while(noer && args[i+1] != NULL && strcmp(args[i+1], "-c")) {
				if(atoi(args[i+1]) < 1) {
					printf(ROJO"Error de sintaxis. Uso: mask <s> -c <cmd [with arguments]>"NEGRO"\n");
					noer = 0;
				} else {
					sigs[i] = atoi(args[i+1]);
				}
				i++;
			}
			if(!noer) continue;
			sigs[i] = 0;
			i++;
			if(args[i] == NULL || strcmp(args[i], "-c") != 0 || args[i+1] == NULL ){
				printf(ROJO"Error de sintaxis. Uso: mask <s> -c <cmd [with arguments]>"NEGRO"\n");
				continue;
			}
			i++;
			while (args[j+i] != NULL) {
				args[j] = strdup(args[i+j]);
				j++;
			}
			args[j] = NULL;
			strcpy(inputBuffer, args[0]);
			
			sigemptyset(&block_masksig);
			
			int k = 0;
			while (sigs[k] != 0) {
				sigaddset(&block_masksig, sigs[k]);
				k++;
			}
			maskable = 1;
		}
		
		//Comando interno time-out
		if(strcmp(inputBuffer, "time-out") == 0) {
			if (args[1] == NULL || args[2] == NULL || atoi(args[1]) < 0) {
				printf(ROJO"Error de sintaxis. Uso: time-out <t> <cmd [with arguments]>"NEGRO"\n");
				continue;
			}
			
			timeo = atoi(args[1]);
			int i = 0;
			while (args[i+2] != NULL) {
			args[i] = strdup(args[i+2]);
			i++;
			}
			args[i] = NULL;
			strcpy(inputBuffer, args[0]);
			
			timeoutable = 1;
		}
		
		//(1) fork a child process using fork()
		pid_fork = fork();
		
		if (pid_fork == -1) {
			perror("ERROR: El proceso hijo no se ha podido crear\n");
		} else if (pid_fork == 0) {  // proceso hijo
			pid_fork = getpid();
			new_process_group(getpid());
			
			if (maskable == 1) { //comando mask
				sigprocmask(SIG_BLOCK, &block_masksig, NULL);
			}
			
			if (timeoutable == 1) { //comando time-out
				pid_timeo = getpgid(pid_fork);
				debug(pid_timeo, "%d");
				int rc = pthread_create(&tid_timeout, NULL, alarm_thread, NULL);
				debug(rc, "%d");
				if(rc) {
					printf("error en el hilo");
				}
			}
			
			if (background == 0) { // si no es background, toma el control
				set_terminal(getpgid(pid_fork));
			}
			
			restore_terminal_signals();
			//(2) the child process will invoke execvp()
			execvp(inputBuffer, args);
			printf(ROJO"Comando '%s' no encontrado\n"NEGRO, inputBuffer);
			exit(-1);
		} else {  // proceso padre
			//(3) if background == 0, the parent will wait, otherwise continue 
			if (background == 0) {
				pid_wait = waitpid(pid_fork, &status, WUNTRACED); // espera al hijo
				set_terminal(getpgid(getpid())); // recupera el terminal
				status_res = analyze_status(status, &info);
				
				if (status_res == SUSPENDED) { // si se ha suspendido, lo añade a la lista
					job *aux = new_job(pid_fork, inputBuffer, args, STOPPED);
					block_SIGCHLD();
					add_job(joblist, aux);
					unblock_SIGCHLD();
					printf(BOLD"Foreground job suspended... pid: %d, command: %s, %s, info: %d\n"REGULAR, 
						pid_wait, inputBuffer, status_strings[status_res], info);
				} else {
				//(4) Shell shows a status message for processed command 
				printf(BOLD"Foreground job exited... pid: %d, command: %s, %s, info: %d\n"REGULAR, 
						pid_wait, inputBuffer, status_strings[status_res], info);
				}

			} else if (respawnable == 1) { // si es respawnable
				job *aux = new_job(pid_fork, inputBuffer, args, RESPAWNABLE);
				block_SIGCHLD();
				add_job(joblist, aux);
				unblock_SIGCHLD();
				printf(BOLD"Respawnable job running... pid: %d, command: %s\n"REGULAR, pid_fork, inputBuffer);
			} else { // si es background
				job *aux = new_job(pid_fork, inputBuffer, args,  BACKGROUND);
				block_SIGCHLD();
				add_job(joblist, aux);
				unblock_SIGCHLD();
				printf(BOLD"Background job running... pid: %d, command: %s\n"REGULAR, pid_fork, inputBuffer);
			}
			
		}
	//(5) loop returns to get_commnad() function
	} // end while
}
