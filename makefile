CFLAGS=-std=c99 -Wall -Wextra -pedantic -O2 -g

.PHONY: all run test

all: pickle

run: pickle
	${TRACE} ./pickle ${FILE}

test: pickle test.tcl
	./pickle test.tcl

main.o: main.c pickle.h block.h

pickle.o: pickle.c pickle.h

block.o: block.c block.h

pickle: pickle.o main.o block.o

clean:
	rm -rf picol pickle *.o *.a
