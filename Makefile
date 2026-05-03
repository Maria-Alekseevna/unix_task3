CC = gcc
CFLAGS = -Wall -Wextra -O2

all: myinit test_prog1 test_prog2 test_prog3

myinit: myinit.c
	$(CC) $(CFLAGS) -o myinit myinit.c

test_prog1: test_prog1.c
	$(CC) $(CFLAGS) -o test_prog1 test_prog1.c

test_prog2: test_prog2.c
	$(CC) $(CFLAGS) -o test_prog2 test_prog2.c

test_prog3: test_prog3.c
	$(CC) $(CFLAGS) -o test_prog3 test_prog3.c

clean:
	rm -f myinit test_prog1 test_prog2 test_prog3

.PHONY: all clean