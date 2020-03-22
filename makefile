# PROJECT: pickle, a TCL like interpreter
# LICENSE: BSD (see 'pickle.c' or 'LICENSE' file)
# SITE:    https://github.com/howerj/pickle
#
VERSION = 0x010001ul
TARGET  = pickle
CFLAGS  = -std=c99 -Wall -Wextra -pedantic -O2 -g -fPIC -fwrapv ${DEFINES} ${EXTRA} -DPICKLE_VERSION="${VERSION}"
AR      = ar
ARFLAGS = rcs
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

# ${TARGET}.1: readme.md
#	pandoc -s -f markdown -t man $< -o $@

install: ${TARGET} lib${TARGET}.a lib${TARGET}.${DLL} ${TARGET}.1 .git
	install -p -D ${TARGET} ${DESTDIR}/bin/${TARGET}
	install -p -m 644 -D lib${TARGET}.a ${DESTDIR}/lib/lib${TARGET}.a
	install -p -D lib${TARGET}.${DLL} ${DESTDIR}/lib/lib${TARGET}.${DLL}
	install -p -m 644 -D ${TARGET}.h ${DESTDIR}/include/${TARGET}.h
	-install -p -m 644 -D ${TARGET}.1 ${DESTDIR}/man/${TARGET}.1
	mkdir -p ${DESTDIR}/src
	cp -a .git ${DESTDIR}/src
	cd ${DESTDIR}/src && git reset --hard HEAD

dist: install
	tar zcf ${TARGET}-${VERSION}.tgz ${DESTDIR}

check:
	scan-build make
	cppcheck --enable=all *.c

clean:
	git clean -dffx

