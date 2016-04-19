build:
	gcc -c server.c
	gcc -c client.c

server: server.o
	gcc -o server server.o -Wall -pthread -lmcrypt

client: client.o
	gcc -o client client.o -Wall -pthread -lmcrypt

all: server client
	@echo "Client and server done."

clean:
	@if [ -e *.txt ]; then rm *.txt; fi
	@if [ -e client ]; then rm client; fi
	@if [ -e server ]; then rm server; fi
	@if [ -e client.o ]; then rm client.o; fi
	@if [ -e server.o ]; then rm server.o; fi

dist:
	@if [ -e lab1b-704588620.tar.gz ]; then rm lab1b-704588620.tar.gz; fi
	@if [ -e README -a -e client.c -a -e server.c -a -e Makefile -a -e my.key ]; then \
		tar -cvzf lab1b-704588620.tar.gz client.c server.c Makefile README my.key; \
	else \
		echo "Error:make dist: the requested files do not exist in the current directory"; \
	fi \
 
.PHONY: all clean dist