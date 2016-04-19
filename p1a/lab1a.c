#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <sys/wait.h>
#include <pthread.h>

#define SHELL_OPTION 's'
#define NULL_OPTION 'n'
#define IMPOSSIBLE_STATUS_CODE -999

/**
 * Lab 1a CS111
 * Author: 704588620
**/

//globals
struct termios termiosp_original, termiosp_new;
struct thread_data {
	int stdin;
	int stdout;
	int shell_write_fd;
	int child_pid;
};

int exit_code = -1;			//global exit code to print at the end of program

 //change fd (terminal) to conform to lab1a specs
void change_terminal_noecho_noncan(int fd, struct termios* t) {
	memset(t, 0, sizeof(*t));
	t->c_lflag &= ~(ICANON|ECHO);
	t->c_cc[VMIN] = 1;	//1 byte requirement for read() to return
	t->c_cc[VTIME] = 0;	//indefinite
	tcsetattr(fd, TCSANOW, t);
}

//reverts terminal back to way it was before any change in this program
void revert_terminal() {
	printf("Reverting terminal to original state...\r\n");
	tcsetattr(STDIN_FILENO, TCSANOW, &termiosp_original);
}

//thread function, read from terminal and echo to shell + stdout
void* thread_read_from_term_echo_to_shell_stdout(void *arg) {
	
	int stdin = ((struct thread_data*)arg)->stdin;
	//int stdout = ((struct thread_data*)arg)->stdout;
	int shellfd = ((struct thread_data*)arg)->shell_write_fd;
	int child_pid = ((struct thread_data*)arg)->child_pid;

	int rc, wc;
	char buff[1];

	while(1) {
		memset(&buff, 0, sizeof(buff));
		rc = read(stdin, buff, sizeof(buff));		//read from terminal
		if(!rc) {
			printf("Something weird happened!!!\r\n");
		}
		if( (buff[0]=='\r') || (buff[0]=='\n')) {
			wc = write(STDOUT_FILENO, "\r\n", strlen("\r\n"));
			if(wc!=2) fprintf(stderr, "ERROR1\n");

			wc = write(shellfd, "\n", 1);
			if(wc!=1) fprintf(stderr, "ERROR2\n");
		}
		else if(buff[0] == 03) {	//interrupt, ^C
			kill(child_pid, SIGINT);//send SIGINT to shell process
		}
		else if(buff[0] == 04) {	//EOF, ^D
			exit_code = 0;	//exit with rc = 0
			kill(child_pid, SIGHUP);//hang up signal	
			close(shellfd);			//close the write pipe, which causes shell to EOF
			break;
		}
		else {	//not a special character, relay to shell and stdout
			wc = write(STDOUT_FILENO, buff, rc);
			if(wc!=rc) fprintf(stderr, "ERROR3\n");

			if((wc = write(shellfd, buff, rc))==-1) {
				perror("thread:write");
				exit(2);//temp
			}
			if(wc!=rc) fprintf(stderr, "ERROR4\n");
		}
	}
	return 0;
}

//basically part 1 of lab1a - read one char at a time, manual echo
void run_blocking_read() {
	//read from terminal
	int read_count = 0;
	while(1) {
		char buff[1];	//can only store one char at a time
		memset(&buff, 0, sizeof(buff));	//zero out just in case

		read_count = read(STDIN_FILENO, buff, sizeof(buff));
		
		if(!read_count) break;		//if something funny happened
		if(buff[0] == 04) break;	//EOF (^D) found
		if( (buff[0] ==  '\r') || (buff[0] == '\n') )  {
			write(STDOUT_FILENO, "\r\n", strlen("\r\n"));	
		}
		else {
			write(STDOUT_FILENO, buff, read_count);
		}
	}
}

//checks the status of the return code from the child, used with waitpid()
//returns the exit status is possible, else -999
int check_child_exit_status(int status) {
	
	int child_status = IMPOSSIBLE_STATUS_CODE;
	if(WIFEXITED(status)) {
		printf("Child terminated normally with code: %d.\r\n",
			WEXITSTATUS(status));

		child_status = WEXITSTATUS(status);
	}
	else if(WIFSIGNALED(status)) {
		printf("Child terminated via signal with code: %d\r\n", 
			WTERMSIG(status));

		//child_status = WTERMSIG(status);
	}
	else if(WIFSTOPPED(status)) {
		printf("Child terminated by stopping of delivery of signal, code: %d\r\n", 
			WSTOPSIG(status));

		//child_status = WSTOPSIG(status);
	}
	else if(WIFCONTINUED(status)) {
		printf("Child resumed by delivery of SIGCONT.\r\n");
	}
	else {
		printf("Child exited weirdly... unexepcted behaviour\r\n");
	}

	return child_status;
}

//signal handler for SIGPIPE
void handle_sigpipe(int signum) {
	exit(1);
}

int main(int argc, char** argv) {

	int oi = 0, c = 0;
	int run_shell = 0;

	//command line arg/opt parsing
	while(1) {

		struct option options[] = {
			{ "shell", 0, 0, SHELL_OPTION },
			{0, 0, 0, NULL_OPTION}
		};	

		c = getopt_long(argc, argv, "", options, &oi);

		if(c==-1) break;	//all options parsed

		switch(c) {
			case SHELL_OPTION:
				run_shell = 1; //set flag
				break;

			default:
				fprintf(stderr, "Invalid option detected\n");
				break;
		}
	}

	//modify state of terminal
	if(tcgetattr(STDIN_FILENO, &termiosp_original) == -1) {	//store defaults
		perror("main():tcgetattr");
		exit(-1);
	}

	atexit(revert_terminal);	//make sure to revert terminal after exit
	signal(SIGPIPE, handle_sigpipe);
	change_terminal_noecho_noncan(STDIN_FILENO, &termiosp_new);	//change termio settings

	if(run_shell) {	//if --shell

		//create two pipes, one for each thread (including this one)
		int pipe1[2];	//pipe that GOES TO SHELL
		int pipe2[2];	//pipe that COMES FROM SHELL

		if((pipe(pipe1)== -1) | (pipe(pipe2) == -1)) {
			perror("main():pipe");
			exit(-1);
		}		

		//fork a child process, use it to exec a shell
		int child_pid = fork();	//child id

		if(child_pid == -1) {
			perror("main():fork");
			exit(-1);
		}
		else if(child_pid == 0) {	//child process!
			//some fd manipulation 
			close(pipe2[0]);		//don't need to read where we write
			close(pipe1[1]);		//don't need to write where we read

			dup2(pipe1[0], STDIN_FILENO);
			close(pipe1[0]);

			dup2(pipe2[1], STDOUT_FILENO);
			dup2(pipe2[1], STDERR_FILENO);
			close(pipe2[1]);

			char* bash_args[2];
			bash_args[0] = "/bin/bash";
			bash_args[1] = NULL;
			execvp(bash_args[0], bash_args);
		}
		else {				//parent!
			//thread will read STDIN and write to pipe1[1]
			//original parent reads from pipe2[0] and writes to STDOUT

			//some fd changes
			close(pipe1[0]);
			close(pipe2[1]);

			//generate a thread and tell it to run the thread function
			//0 returned on success!
			pthread_t thread_id;
			struct thread_data my_thread;
			memset(&my_thread, 0, sizeof(struct thread_data));
			//my_thread.stdin = (int)pipe2[0];
			//my_thread.stdout = STDOUT_FILENO;
			my_thread.stdin = STDIN_FILENO;
			my_thread.stdout = STDIN_FILENO;
			my_thread.shell_write_fd = pipe1[1];	//write to shell
			my_thread.child_pid = child_pid;
			//printf("Parent: stdin for thread: %d\r\n", my_thread.stdin);
			if(pthread_create(&thread_id, NULL, &thread_read_from_term_echo_to_shell_stdout,
				(void *)&my_thread)) {
				perror("main():pthread_create");
				exit(-1);
			}

			int status;		//used in waitpid

			//read from shell, write to stdout
			int read_count = 0;
			while(1) {
				char buff[1];
				memset(&buff, 0, sizeof(buff));
				read_count = read(pipe2[0], buff, sizeof(buff));	//read from shell
				if(!read_count) {
					//^D, EOF from shell, note there are possible causes!
					printf("EOF received from shell\r\n");
					if(exit_code < 0) {
						pthread_cancel(thread_id);	//ask thread to terminate, comment out to allow sigpipe
						exit_code = 1;	//default
					}
					break;
				}
				else {
					int wc = write(STDOUT_FILENO, buff, read_count);
					if(wc != read_count) {
						fprintf(stderr, "Error in write:284\n");
						exit(-1);
					}
				}
			}


			//wait for thread to finish
			pthread_join(thread_id, 0);

			//clean up remaining FDs, used by parent and thread
			close(pipe1[1]);
			close(pipe2[0]);

			//print status of child/shell
			waitpid(child_pid, &status, 0);
			status = check_child_exit_status(status);
			if(status == IMPOSSIBLE_STATUS_CODE) {
				printf("Child exit status not available (e.g. child terminated by signal).\r\n");
			}else {
				printf("Exit code for shell: %d\r\n", status);
			}
			
			//exit with determined exit code
			exit(exit_code);
		}

	}
	else {	//no --shell, generic read and echo
		run_blocking_read();
	}

	
	return 0;
}

