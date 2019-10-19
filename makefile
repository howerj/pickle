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

VERSION = 0x010000ul
CFLAGS  = -std=c99 -Wall -Wextra -pedantic -O2 -g -fPIC -fwrapv ${DEFINES} ${EXTRA} -DPICKLE_VERSION="${VERSION}"
AR      = ar
ARFLAGS = rcs
TARGET  = pickle
TRACE   =
DESTDIR = install

ifeq ($(OS),Windows_NT)
DLL=dll
else # Assume Unixen
DLL=so
endif

.PHONY: all run test clean install dist

all: ${TARGET}

run: ${TARGET}
	${TRACE} ./${TARGET} ${FILE}

test: ${TARGET} unit.tcl
	./${TARGET} -t
	./${TARGET} -a unit.tcl

main.o: main.c ${TARGET}.h block.h

${TARGET}.o: ${TARGET}.c ${TARGET}.h

block.o: block.c block.h

unit: lib${TARGET}.a block.o unit.o

lib${TARGET}.a: ${TARGET}.o
	${AR} ${ARFLAGS} $@ $<

lib${TARGET}.${DLL}: ${TARGET}.o ${TARGET}.h
	${CC} ${CFLAGS} -shared ${TARGET}.o -o $@

${TARGET}: main.o block.o lib${TARGET}.a
	${CC} ${CFLAGS} $^ -o $@

# pickle.1: readme.md
#	pandoc -s -f markdown -t man $< -o $@

install: ${TARGET} lib${TARGET}.a lib${TARGET}.${DLL} pickle.1
	install -p -D ${TARGET} ${DESTDIR}/bin/${TARGET}
	install -p -m 644 -D lib${TARGET}.a ${DESTDIR}/lib/lib${TARGET}.a
	install -p -D lib${TARGET}.${DLL} ${DESTDIR}/lib/lib${TARGET}.${DLL}
	install -p -m 644 -D pickle.h ${DESTDIR}/include/pickle.h
	install -p -m 644 -D pickle.1 ${DESTDIR}/man/pickle.1
	mkdir -p ${DESTDIR}/src
	install -p -m 644 -D pickle.c pickle.h unit.c block.c block.h main.c unit.tcl LICENSE readme.md makefile -t ${DESTDIR}/src

dist: install
	tar zcf ${TARGET}-${VERSION}.tgz ${DESTDIR}



check:
	scan-build make
	cppcheck --enable=all *.c

clean:
	git clean -dfx

