CC=gcc
CFLAGS=-std=c99 -Wall -Wextra -pedantic -O2 -g -fwrapv
AR=ar
ARFLAGS=rcs
RANLIB=ranlib

.PHONY: all run test wrap clean

all: pickle

run: pickle
	${TRACE} ./pickle ${FILE}

wrap: pickle
	rlwrap ./pickle -a ${FILE}

test: pickle unit.tcl
	./pickle -t
	./pickle -a unit.tcl

main.o: main.c pickle.h block.h

pickle.o: pickle.c pickle.h

block.o: block.c block.h

libpickle.a: pickle.o
	${AR} ${ARFLAGS} $@ $<
	${RANLIB} $@

pickle: main.o block.o libpickle.a
	${CC} ${CFLAGS} $^ -o $@
	# strip $@

dict: dict.o

clean:
	rm -rf picol pickle dict *.o *.a
