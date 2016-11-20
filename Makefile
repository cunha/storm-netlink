all: storm.c
	gcc -Wall -Wpedantic -std=gnu11 storm.c -o storm

clean:
	rm -f storm


