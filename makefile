# PROJECT: pickle, a TCL like interpreter
# LICENSE: BSD (see 'pickle.c' or 'LICENSE' file)
# SITE:    https://github.com/howerj/pickle
#
# NOTES: 
# * We could detect the OS, and whether 'rlwrap' exists on the system, it is
# not worth the complexity however. See 
# <https://stackoverflow.com/questions/5618615> and 
# <https://stackoverflow.com/questions/714100>
# * This makefile should be kept as simple as possible.
# * Use the ${DEFINES} macro to turn on/off options in the program. 

# <https://news.ycombinator.com/item?id=15400396>
# EXTRA = -Wduplicated-cond -Wlogical-op \
#	-Wnull-dereference -Wjump-misses-init \
#	-Wshadow 

CFLAGS  = -std=c99 -Wall -Wextra -pedantic -O2 -g -fwrapv ${DEFINES} ${EXTRA}
AR      = ar
ARFLAGS = rcs
RANLIB  = ranlib
TARGET  = pickle
TRACE   =

.PHONY: all run test clean dist

all: ${TARGET}

run: ${TARGET}
	${TRACE} ./${TARGET} ${FILE}

test: ${TARGET} unit.tcl
	./${TARGET} -t
	./${TARGET} -a unit.tcl

main.o: main.c ${TARGET}.h block.h

${TARGET}.o: ${TARGET}.c ${TARGET}.h

block.o: block.c block.h

simple: lib${TARGET}.a simple.o

lib${TARGET}.a: ${TARGET}.o
	${AR} ${ARFLAGS} $@ $<
	${RANLIB} $@

${TARGET}: main.o block.o lib${TARGET}.a
	${CC} ${CFLAGS} $^ -o $@

check:
	cppcheck --enable=all *.c

clean:
	git clean -dfx

SHELL := sh
date  := $(shell date "+%Y%m%d%H%M%S")
dist:
	tar zcf ../${TARGET}-${date}.tgz ../${TARGET}

