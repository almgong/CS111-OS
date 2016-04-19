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

/**
* Lab 0 CS111
*
* Author: Allen Gong
**/

//handler for signal() call
void handle_segfault(int signum) {
	//received the signal
	if(signum==SIGSEGV) {
		fprintf(stderr, "SIGSEGV caught, exiting...\n");
		_exit(3);
	}
}

int main(int argc, char** argv) {

	//variables used
	int fd_in = 0; 	//to read from
	int fd_out = 1;	//to write to
	int c, fd_temp;
	int oi = 0;
	int cause_segfault = 0;	//bool flag set to true/1 only if --segfault

	//infinite loop, only breaks once all command line option arguments are parsed
	//main idea is that this loop acts as a preparation step before any i/o
	while(1) {
		//reset options
		struct option possible_options[] = {
			{"input", 1, 0, 0},
			{"output", 1, 0, 0},
			{"segfault", 0, 0, 0},
			{"catch", 0, 0, 0}
		};

		//if no more option arguments, break out of loop
		if((c = getopt_long_only(argc, argv, "", possible_options, &oi)) == -1)
			break;

		//if logic got to here, an option arg was successfuly retrieved
		switch(c) {
			case 0:	//a valid argument found
				//if input was the option
				if(strcmp(possible_options[oi].name, "input") == 0) { 
					if((fd_temp = open(optarg, O_RDONLY)) == -1) {
						//error occurred
						int err_num = errno;	//save errno
						perror("main():input");
						fprintf(stderr, "There was an error with code: %d\n", err_num);
						_exit(1);
					}

					//file was successfuly opened, lets now redirect to fd_in
					close(fd_in);	//close 0, stdin
					dup(fd_temp);	//fills in fd_in with ref
					close(fd_temp);	//no longer need this
				}
				else if(strcmp(possible_options[oi].name, "output") == 0) {
					//create the file, for simplicity, have permissions be lax
					if((fd_temp = creat(optarg, 0666)) == -1) {
						//error occurred
						int err_num = errno;
						perror("main():output");
						fprintf(stderr, "There was an error with code: %d\n", err_num);
						_exit(2);
					}

					//redirect to fd_out
					close(fd_out);
					dup(fd_temp);	//fd_out now is pointing to the created fd
					close(fd_temp);
				}
				else if(strcmp(possible_options[oi].name, "segfault") == 0) {
					//create segfault after prep
					cause_segfault = 1;
				}
				else if(strcmp(possible_options[oi].name, "catch") == 0) {
					if(signal(SIGSEGV, handle_segfault) == SIG_ERR) {
						//error in signaling
						perror("main():catch");
						_exit(5);
					}
				}
				else {
					//intentionally left blank, should not be possible to reach
				}

				break;

			case '?':	//unknown arg found
				printf("An invalid argument found, skipping...\n");
				break;

			default:	//for completeness, include a default
				break;
		}
	}	

	//if --segfault
	if(cause_segfault) {
		char* null_ptr = NULL;
		*null_ptr = 'a';	//should generate segfault
	}

	//main logic, simply read from fd_in and write to fd_out
	char buff[500];		
	int read_count = 0;
	int write_count = 0;
	//read 500 bytes at a time, write to fd_out
	while(1) {

		//read 500 bytes into buffer
		if((read_count = read(fd_in, buff, sizeof(buff))) == -1) {
			//error occurred
			int err_num = errno;
			perror("main()");
			fprintf(stderr, "Error occurred with code: %d\n", err_num);
			_exit(4);
		}
		if(read_count == 0) {
			break;
		}
		
		write_count = write(fd_out, buff, read_count);

		if(write_count < read_count) {
			//error has occurred, maybe ran out of disk space, etc.
			perror("main():write");
			_exit(6);
		}

		//incremement offset for next write
		lseek(fd_out, 0, SEEK_END);	//seek fd to eof for next write

	}

	close(fd_in);
	close(fd_out);
	_exit(0);	//successful termination

	return 0;
} 


