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
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <mcrypt.h>

#define PORT_OPTION 'p'
#define NULL_OPTION 'n'
#define ENCRYPT_OPTION 'e'
#define ENC_KEY_SIZE 16

//globals
int exit_code = 0;	//default exit code of 0
struct termios termiosp_original, termiosp_new;
struct thread_data {
	int stdin;
	int stdout;
};
int newsockfd, to_encrypt;	//on accept of new connection, flag
int pipe1[2];	//pipe that GOES TO SHELL
int pipe2[2];	//pipe that COMES FROM SHELL

//encryption variables
MCRYPT td;				//crypt module
char* IV = NULL;		//init vector 
char key[ENC_KEY_SIZE];

void init_IV() {
	if(IV!=NULL) free(IV);
	IV = malloc(mcrypt_enc_get_iv_size(td));
	int i;
	for (i = 0; i < mcrypt_enc_get_iv_size(td); i++) {
    	IV[i] = rand();
  	}
}
void init_encryption() {
	memset(key, 0, sizeof(key));			//zero out the key store
	int keyfd = open("my.key", O_RDONLY);	
	int rc = read(keyfd, key, ENC_KEY_SIZE);
	if(rc<=0 || rc!=12) {//12bytes in my.key
		fprintf(stderr, "%s%d\n", "Something weird happened while reading my.key.", rc);
		exit(-1);
	}
	
	td = mcrypt_module_open("twofish", NULL, "cfb", NULL);
	if(td==MCRYPT_FAILED) {
		fprintf(stderr, "%s\n", "Error opening mcrypt.");
		exit(-1);
	}
	srand(100);	//seed rand() with 100
	int i;
	init_IV();
  	i = mcrypt_generic_init( td, key, ENC_KEY_SIZE, IV);
  	if(i < 0) {
  		fprintf(stderr, "%s\n", "Error in mycrypt_generic_init");
  	} 

  	close(keyfd);
}

//thread function to read output from shell and to send to socket(stdin)
void* thread_read_from_shell_to_socket(void*arg) {
	int stdin = ((struct thread_data*)arg)->stdin;
	int stdout = ((struct thread_data*)arg)->stdout;
	int wc, rc;
	char buff[1];

	//read from shell
	while(1) {
		memset(buff, 0, sizeof(buff));
		rc = read(stdin, buff, sizeof(buff));
		if(rc <= 0) {
			//shell EOF, or error
			exit(2);
		}
		if(to_encrypt) mcrypt_generic(td, buff, sizeof(buff));
		wc = write(stdout, buff, sizeof(buff));
		if(wc<0 || wc != rc) {
			perror("write in thread");
			break;
		}

	}

}

//closes open fds
void clean_up() {
	if(IV!=NULL) {	//encryption-decryption occured!
		free(IV);
		mcrypt_generic_end(td);
	}
	//close fds (server)
	close(pipe1[1]);
	close(pipe2[0]);
	close(newsockfd);
}

//signal handler for SIGPIPE
void handle_sigpipe(int signum) {
	exit(2);
}

/**
 * Server program
 *
 * Author: 704588620
**/
int main(int argc, char** argv) {

	int oi = 0, c = 0;
	char* port_desired;

	//command line arg/opt parsing
	while(1) {

		struct option options[] = {
			{ "port", required_argument, 0, PORT_OPTION },
			{ "encrypt", 0, 0, ENCRYPT_OPTION },
			{0, 0, 0, NULL_OPTION}
		};	

		c = getopt_long(argc, argv, "", options, &oi);

		if(c==-1) break;	//all options parsed

		switch(c) {

			case PORT_OPTION:
				port_desired = (char*)optarg;
				break;

			case ENCRYPT_OPTION:
				to_encrypt = 1;
				init_encryption();
				break;

			default:
				fprintf(stderr, "Invalid option detected\n");
				break;
		}
	}

	//set up TCP socket
	int port_num = atoi(port_desired);		//port as int
	int sockfd, client_addr_len;			//misc
	struct sockaddr_in serv_addr, cli_addr;	//addresses
	memset(&serv_addr, 0, sizeof(struct sockaddr_in));
	memset(&cli_addr, 0, sizeof(struct sockaddr_in));
	atexit(clean_up);	//clean up code before exit

	//create socket to listen in
	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "%s\n", "Error in creating socket.");
		exit(-1);
	}

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(port_num);

	//bind the socket to the desired port, and listen with queue=1
	if(bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		perror("bind");
		exit(-1);
	}
	if(listen(sockfd, 1) < 0) {
		perror("listen");
		exit(-1);
	}

	//accept incoming connect()
	client_addr_len = sizeof(cli_addr);
	if((newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, 
		&client_addr_len)) < 0 ) {
		perror("accept");
		exit(-1);
	}
	close(sockfd);	//no more connections allowed
	//connection has been made, fork a child shell//

	//create two pipes, one for each thread (including this one)
	if((pipe(pipe1)== -1) | (pipe(pipe2) == -1)) {
		perror("pipe");
		exit(-1);
	}		

	//fork a child process, use it to exec a shell
	int child_pid = fork();	//child id

	if(child_pid == -1) {
		perror("fork");
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
	else { //is the parent process!
		//fd manipulation
		close(pipe1[0]);	//don't read where we will write
		close(pipe2[1]);	//don't write where we read

		signal(SIGPIPE, handle_sigpipe);

		//redirect stdout, stdin, and stderr to the socket
		dup2(newsockfd, STDOUT_FILENO);
		dup2(newsockfd, STDIN_FILENO);
		dup2(newsockfd, STDERR_FILENO);

		//prepare thread for reading output from shell and writing it socket
		pthread_t thread_id;
		struct thread_data my_thread;
		memset(&my_thread, 0, sizeof(struct thread_data));
		my_thread.stdin = pipe2[0];
		my_thread.stdout = newsockfd;
		if(pthread_create(&thread_id, NULL, &thread_read_from_shell_to_socket,
		(void *)&my_thread)) {
			perror("main():pthread_create");
			exit(-1);
		}

		char buff[1];
		int rc, wc;
		
		//read from socket, write to shell process
		while(1) {
			//read from socket
			rc = read(newsockfd, buff, sizeof(buff));
			if(rc <= 0) {	//EOF/error
				kill(child_pid, SIGKILL);		//kill shell
				int status;
				waitpid(child_pid, &status, 0);	//wait for termination
				exit_code = 1;
				break;
			}

			//write to shell (SIGPIPE possible)
			if(to_encrypt) mdecrypt_generic(td, buff, sizeof(buff));	//decrypt if needed
			wc = write(pipe1[1], buff, rc);
			if(wc<0 || wc!=rc) {
				if(errno == EPIPE) {
					exit_code = 2; 
					break;
				}
				else {
					perror("write");
					exit(-1);
				}
			}

			if(to_encrypt) {	//re-init for next input from socket
				mcrypt_generic_end(td);
				memset(td, 0, sizeof(td));
				td = mcrypt_module_open("twofish", NULL, "cfb", NULL);
				if(td==MCRYPT_FAILED) {
					fprintf(stderr, "%s\n", "Error opening mcrypt.");
					exit(-1);
				}
				//init_IV();
				mcrypt_generic_init( td, key, ENC_KEY_SIZE, IV);
			}
		}

		pthread_cancel(thread_id);	//ask thread nicely to exit
		pthread_join(thread_id, 0); //wait for thread to exit
	}

	//note fds are closed in atexit function (including newsockfd)!
	exit(exit_code);
	return 0;
}


