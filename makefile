CFLAGS=-std=c99 -Wall -Wextra -pedantic -O2
all: pickle picol

run: pickle
	./pickle

pickle: pickle.o main.o

picol: picol.orig.c

clean:
	rm -rf picol pickle *.o *.a
