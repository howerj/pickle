CFLAGS=-std=c99 -Wall -Wextra -pedantic -O2 -g
all: pickle picol

run: pickle
	${TRACE} ./pickle ${FILE}

main.o: main.c pickle.h

pickle.o: pickle.c pickle.h

block.o: block.c block.h

block: block.o

pickle: pickle.o main.o

picol: picol.orig.c
	${CC} $< -o $@

clean:
	rm -rf picol pickle *.o *.a
