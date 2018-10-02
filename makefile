CFLAGS=-std=c99 -Wall -Wextra -pedantic
all: pickle picol

run: pickle
	./pickle

picol: picol.orig.c

clean:
	rm -rf picol pickle *.o *.a
