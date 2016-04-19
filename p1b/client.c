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

#define LOG_OPTION 'l'
#define PORT_OPTION 'p'
#define NULL_OPTION 'n'
#define ENCRYPT_OPTION 'e'
#define SERVER_HOSTNAME "localhost"
#define ENC_KEY_SIZE 16

//globals
struct termios termiosp_original, termiosp_new;
struct thread_data {
	int stdin;
	int stdout;
	char* logfile;	//NULL if no --log'ging needed
};

int logfilefd = -1;	//fd for the logging file, used as needed
int sockfd;
int to_encrypt;

//encryption variables
MCRYPT td;				//crypt module
char* IV = NULL;		//init vector 
char key[ENC_KEY_SIZE];


//initializes and readies all encryption related variables
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

//thread function, read from socket(stdin) and echo to STDOUT
//log only "RECEIVED: " messages
void* thread_read_from_socket_to_stdout(void* arg) {
	int stdin = ((struct thread_data*)arg)->stdin;
	int stdout = ((struct thread_data*)arg)->stdout;
	char* logfile = ((struct thread_data*)arg)->logfile;

	int rc, wc;
	char buff[1];
	memset(buff, 0, sizeof(buff));
 	while(1) {

 		rc = read(stdin, buff, sizeof(buff));
 		if(rc <= 0) {
 			//on error or EOF from socket!
 			exit(1);
 		}

 		if(logfile) {	//if logfile is not null, write to the file!
 			char msg[100]; 	//more than necessary
			int pc = sprintf(msg, "RECEIVED %d bytes: %s\n", rc, buff);
			wc = write(logfilefd, msg, pc);
 		}

 		if(to_encrypt) mdecrypt_generic(td, buff, sizeof(buff));	//if needed

 		//echo to terminal
 		wc = write(stdout, buff, rc);
 		if(wc<0 || wc != rc) {
 			fprintf(stderr, "%s\n", "Error in write in thread.");
 			break;
 		}

 	}

	return 0;
}

//change fd (terminal) to conform to lab1a specs
void change_terminal_noecho_noncan(struct termios* t) {
	memset(t, 0, sizeof(*t));
	t->c_lflag = ISIG;	//allow ^C to exit program
	t->c_lflag &= ~(ICANON|ECHO);
	t->c_cc[VMIN] = 1;	//1 byte requirement for read() to return
	t->c_cc[VTIME] = 0;	//indefinite
	t->c_cc[VINTR] = 3;	//^C
	tcsetattr(STDIN_FILENO, TCSANOW, t);
}

//reverts terminal back to way it was before any change in this program
//*edit, also closes any used fds
void revert_terminal() {
	if(IV!=NULL) {	//means encryption occurred!
		free(IV);
		mcrypt_generic_end(td);
	}
	if(logfilefd > 0) close(logfilefd);
	close(sockfd);
	tcsetattr(STDIN_FILENO, TCSANOW, &termiosp_original);
}

/**
 * Client program
 *
 * Author: 704588620
**/
int main(int argc, char** argv) {

	int oi = 0, c = 0;
	int log_comm = 0;
	to_encrypt = 0;	
	char* port_desired;
	char* log_arg;
	//command line arg/opt parsing
	while(1) {

		struct option options[] = {
			{ "log", required_argument, 0, LOG_OPTION },
			{ "port", required_argument, 0, PORT_OPTION },
			{ "encrypt", 0, 0, ENCRYPT_OPTION },
			{0, 0, 0, NULL_OPTION}
		};	

		c = getopt_long(argc, argv, "", options, &oi);

		if(c==-1) break;	//all options parsed

		switch(c) {
			case LOG_OPTION:
				log_comm = 1; //set flag
				log_arg = (char*)optarg;
				//open the file for logging
				if((logfilefd = open(optarg, O_WRONLY | O_APPEND | O_CREAT, 0666)) == -1) {
					//error occurred
					perror("open");
					exit(-1);
				}

				break;

			case PORT_OPTION:
				port_desired = (char*)optarg;
				break;

			case ENCRYPT_OPTION:
				to_encrypt = 1;
				init_encryption();	//initialize needed variables
				break;

			default:
				fprintf(stderr, "Invalid option detected\n");
				break;
		}
	}


	//some init
	//get state of terminal
	if(tcgetattr(STDIN_FILENO, &termiosp_original) == -1) {	//store defaults
		perror("main():tcgetattr");
		exit(-1);
	}
	atexit(revert_terminal);	//make sure to revert terminal after exit
	change_terminal_noecho_noncan(&termiosp_new);	//change termio settings


	//ready for client logic now, terminal set up.
	char buff[1];	//size one buffer
	memset(buff, 0, sizeof(buff));
	int rc, wc;		//returns of read() and write()
	int exit_code;	//exit code of client depending on program execution
	int cont = 1;	//controls the main loop below

	//set up TCP connection to server
	int portnum = atoi(port_desired);
	struct sockaddr_in server_addr;
	struct hostent *server;
	memset(&server_addr, 0, sizeof(server_addr));

	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {		//create socket
		fprintf(stderr, "%s\n", "Error: Socket creation failed!");
		exit(-1);
	}

	server_addr.sin_family = AF_INET;					//init server addr info
	server_addr.sin_port = htons(portnum);
	server = gethostbyname(SERVER_HOSTNAME);	//server runs on localhost
	bcopy((char *)server->h_addr, (char *)&server_addr.sin_addr.s_addr,
         server->h_length);
	if(connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		perror("connect()");
		fprintf(stderr, "\r%s\r\n", "Error: Could not connect to server");
		exit(-1);
	}

	//generate a thread and tell it read from the socket!
	pthread_t thread_id;
	struct thread_data my_thread;
	memset(&my_thread, 0, sizeof(struct thread_data));
	my_thread.stdin = sockfd;
	my_thread.stdout = STDOUT_FILENO;
	if(log_comm) my_thread.logfile = log_arg;
	else my_thread.logfile = NULL;

	if(pthread_create(&thread_id, NULL, &thread_read_from_socket_to_stdout,
		(void *)&my_thread)) {
		perror("main():pthread_create");
		exit(-1);
	}

	//reads from terminal and sends to server (socket) only - NO READs from socket
	//log only "SENT:" messages
	while(cont) {
		rc = read(STDIN_FILENO, buff, sizeof(buff));	//one char at a time
		if(rc==0 || rc < 0) {
			exit_code = 1;	//EOF from terminal OR read error --might not be right
			break;
		}
		else if(buff[0]==04) {
			printf("Escape key ^D pressed, exiting now!\r\n");
			exit_code = 0;
			break;
		}
		else if(buff[0]=='\r' || buff[0]=='\n') {
			wc = write(STDOUT_FILENO, "\r\n", 2);
			if(wc < 0 || wc != 2) {
				fprintf(stderr, "%s\n", "Error in write to stdout.");
				exit(-1);
			}

			//code to send buffer contents through socket
			buff[0] = '\n';
			if(to_encrypt) mcrypt_generic(td, buff, sizeof(buff));
			wc = write(sockfd, buff, sizeof(buff));

			if(wc < 0 || wc!=1) {
				fprintf(stderr, "%s\n", "Error in write to socket.");
				exit(-1);
			}
		}
		else {	//a regular character typed in, echo to STDOUT and to socket
			wc = write(STDOUT_FILENO, buff, rc);
			if(wc < 0 || wc != rc) {
				fprintf(stderr, "%s\n", "Error in write to stdout-2.");
				exit(-1);
			}

			//code to send buffer contents through socket
			if(to_encrypt) mcrypt_generic(td, buff, sizeof(buff));
			wc = write(sockfd, buff, rc);
			if(wc < 0 || wc != rc) {
				fprintf(stderr, "%s\n", "Error in write to socket-2.");
			}
		}

		//regardless, if --log==<filename>, then log buff to the logfile
		if(log_comm) {
			char msg[100]; 	//more than necessary
			int pc = sprintf(msg, "SENT %d bytes: %s\n", rc, buff);
			wc = write(logfilefd, msg, pc);
			if(wc<=0) {
				printf("%s\n", "Failed write...");
			}
		}

		if(to_encrypt) {
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


	//prepare for program end, recall atexit() set to clean up fds
	pthread_cancel(thread_id);	//ask thread nicely to exit
	pthread_join(thread_id, 0); //wait for thread to exit
	exit(exit_code);
	
	return 0;
}

