CFLAGS=-std=c99 -Wall -Wextra -pedantic -O2 -g -fwrapv ${DEFINES}
AR=ar
ARFLAGS=rcs
RANLIB=ranlib

.PHONY: all run test wrap clean dist

all: pickle

run: pickle
	${TRACE} ./pickle ${FILE}

test: pickle unit.tcl
	./pickle -t
	./pickle -a unit.tcl

main.o: main.c pickle.h block.h

pickle.o: pickle.c pickle.h

block.o: block.c block.h

simple: libpickle.a simple.o

libpickle.a: pickle.o
	${AR} ${ARFLAGS} $@ $<
	${RANLIB} $@

pickle: main.o block.o libpickle.a
	${CC} ${CFLAGS} $^ -o $@

check:
	cppcheck --enable=all *.c

clean:
	git clean -dfx

SHELL := sh
date := $(shell date "+%Y%m%d%H%M%S")
dist:
	tar zcf ../pickle-${date}.tgz ../pickle

