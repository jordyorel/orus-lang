CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS = -lm

all: test_register_vm

test_register_vm: test_register.c vm.c vm.h
	$(CC) $(CFLAGS) -o test_register test_register.c vm.c $(LDFLAGS)

clean:
	rm -f test_register *.o

run: test_register
	./test_register

debug: test_register_vm
	ORUS_TRACE=1 ./test_register

.PHONY: all clean run debug