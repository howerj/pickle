CFLAGS=-std=c99 -Wall -Wextra -pedantic -O2 -g
all: pickle

run: pickle
	${TRACE} ./pickle ${FILE}

main.o: main.c pickle.h

pickle.o: pickle.c pickle.h

block.o: block.c block.h

pickle: pickle.o main.o block.o

clean:
	rm -rf picol pickle *.o *.a
