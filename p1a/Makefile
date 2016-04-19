lab1a: lab1a.o
	gcc -Wall -o lab1a -g lab1a.c -pthread

clean:
	@if [ -e lab1a ]; then rm lab1a; fi
	@if [ -e lab1a.o ]; then rm lab1a.o; fi

dist: 
	@if [ -e lab1a-704588620.tar.gz ]; then rm lab1a-704588620.tar.gz; fi
	@if [ -e README -a -e lab1a.c -a -e Makefile ]; then \
		tar -cvzf lab1a-704588620.tar.gz lab1a.c Makefile README; \
	else \
		echo "Error:make dist: the requested files do not exist in the current directory"; \
	fi \
