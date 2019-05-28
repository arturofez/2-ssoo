/**
UNIX Shell Project
Author: Arturo Fernandez Perez

Sistemas Operativos
Grados I. Informatica, Computadores & Software
Dept. Arquitectura de Computadores - UMA

Some code adapted from "Fundamentos de Sistemas Operativos", Silberschatz et al.

To compile and run the program:
   $ gcc Shell_project.c job_control.c -o Shell
   $ ./Shell          
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

// ---------------------------------------------------------------------
//                             MANEJADOR
// ---------------------------------------------------------------------

	void manejador(int senal)
	{
		//int pid = getpid();
		int pid, status, info;
		enum status status_res;
		job * jobs = joblist->next;
		
		block_SIGCHLD(); // se bloquea la señal SIGCHLD cuando se accede a la lista de procesos
		while (jobs != NULL){
			pid = waitpid(jobs->pgid, &status, WUNTRACED | WNOHANG | WCONTINUED);
			status_res = analyze_status(status, &info);
			//debug(status,%d);
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
	
	joblist = new_list("Job list"); // inicializo la lista de procesos
	
	ignore_terminal_signals();
	signal(SIGCHLD, manejador); // capturar señal SIGCHLD y manejarla
	
	while (1)   /* Program terminates normally inside get_command() after ^D is typed*/
	{   		
		printf("COMMAND->");
		fflush(stdout);
		get_command(inputBuffer, MAX_LINE, args, &background);  /* get next command */
		
		if(args[0]==NULL) continue;   // if empty command
		
		// Comando internos cd
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
					job *aux = new_job(pid, com, STOPPED);
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
			} else if (aux->state == BACKGROUND){
				printf(BOLD"El proceso %d no está suspendido... pid: %d, command: %s"REGULAR"\n",
					n, aux->pgid, aux->command);
			} else {
				aux->state = BACKGROUND;
				killpg(aux->pgid,SIGCONT);
				printf(BOLD"Suspended job %d resumed... pid: %d, command: %s"REGULAR"\n",
						n, aux->pgid, aux->command);
			}
			continue;
		}
		
		//(1) fork a child process using fork()
		pid_fork = fork();
		
		if (pid_fork == -1) {
			perror("ERROR: El proceso hijo no se ha podido crear\n");
		} else if (pid_fork == 0) {  // proceso hijo
			pid_fork = getpid();
			new_process_group(getpid());
			
			if (background == 0) { // si no es background, toma el control
				set_terminal(getpgid(pid_fork));	
			}

			restore_terminal_signals();
			//(2) the child process will invoke execvp()
			execvp(inputBuffer, args);
			printf(ROJO"Comando '%s' no encontrado\n"NEGRO, inputBuffer);
			exit(-1);
		} else {  // proceso padre
			new_process_group(pid_fork);
			//(3) if background == 0, the parent will wait, otherwise continue 
			if (background == 0) { // si no es background
				pid_wait = waitpid(pid_fork, &status, WUNTRACED); // espera al hijo
				set_terminal(getpgid(getpid())); //recupera el terminal
				status_res = analyze_status(status, &info);

				if (status_res == SUSPENDED) { // si se ha suspendido, lo añade a la lista
					job *aux = new_job(pid_fork, inputBuffer, STOPPED);
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

			} else { // si es background
				job *aux = new_job(pid_fork, inputBuffer, BACKGROUND);
				block_SIGCHLD();     
				add_job(joblist, aux);
				unblock_SIGCHLD();
				printf(BOLD"Background job running... pid: %d, command: %s\n"REGULAR, pid_fork, inputBuffer);
			}
			
		}
	//(5) loop returns to get_commnad() function
	} // end while
}
