lab0: lab0.o
	gcc -Wall -o lab0 -g lab0.c 

check: lab0
	@echo "smoke-test: Beginning smoke-test..."
	@echo "smoke-test: this is a test file." > testinput.txt

	@echo "smoke-test: start test 1 correct args" 
	@if ./lab0 --input=testinput.txt --output=testoutput.txt; then \
		echo "smoke-test: PASS: write!"; \
	else \
		echo "smoke-test: Error in write" \
	; fi \

	@if diff -q "testoutput.txt" "testinput.txt"; then \
		echo "smoke-test: test 1 correct in/output SUCCESS!\n"; \
	else \
		echo "smoke-test: test 1 FAILED\n" \
	; fi

	@echo "smoke-test: start test 2 error codes"
	@if ./lab0 --input=nonexistingfile.txt; then echo "smoke-test: ERROR - invalid input file\n"; \
	elif test $$? -eq 1; then echo "smoke-test: PASS - invalid input file\n"; \
	else echo "smoke-test: ERROR - invalid input file\n"; fi

	@if ./lab0 --input=testinput.txt --output=fakedir/fail.txt; \
		then echo "smoke-test: ERROR - invalid output file\n"; \
	elif test $$? -eq 2; then echo "smoke-test: PASS - invalid output file\n"; \
	else echo "smoke-test: ERROR - invalid output file\n"; fi

	@if ./lab0 --segfault --catch; \
		then echo "smoke-test: ERROR - catch\n"; \
	elif test $$? -eq 3; then echo "smoke-test: PASS - catch\n"; \
	else echo "smoke-test: ERROR - catch\n"; fi

	@if ./lab0 --segfault; \
		then echo "smoke-test: ERROR - segfault\n"; \
	elif test $$? -eq 139; then echo "smoke-test: PASS - segfault\n"; \
	else echo "smoke-test: ERROR - segfault\n"; fi

	@echo "All PASS = test 2 success"
	@echo "smoke-test: complete, please check all cases PASS\n"
	
clean: 
	@echo "make clean: Returning directory to post-unzip state..."
	@if [ -e lab0.o ]; then rm lab0.o; fi 
	@if [ -e lab0 ]; then rm lab0; fi
	@if [ -e testinput.txt ]; then rm testinput.txt; fi
	@if [ -e testoutput.txt ]; then rm testoutput.txt; fi
	@echo "make clean: Done!"

dist:
	@echo "make dist: Generating distribution tarball"
	@echo "make dist: files included:"
	@if [ -e lab0-704588620.tar.gz ]; then rm lab0-704588620.tar.gz; fi
	@tar -cvzf lab0-704588620.tar.gz lab0.c Makefile README segfault_backtrace_nobp.png segfault_withbp_inspect.png

