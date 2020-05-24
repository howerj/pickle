# PROJECT: pickle, a TCL like interpreter
# LICENSE: BSD (see 'pickle.c' or 'LICENSE' file)
# SITE:    https://github.com/howerj/pickle
#
VERSION = 0x040104ul
TARGET  = pickle
CFLAGS  = -std=c99 -Wall -Wextra -pedantic -O2 -fwrapv ${DEFINES} ${EXTRA} -DPICKLE_VERSION="${VERSION}"
AR      = ar
ARFLAGS = rcs
TRACE   =
DESTDIR = install

.PHONY: all run test clean install dist profile

all: ${TARGET}

run: ${TARGET} shell
	${TRACE} ./${TARGET} shell

test: ${TARGET} shell
	${TRACE} ./${TARGET} shell -t

main.o: main.c ${TARGET}.h

${TARGET}.o: ${TARGET}.c ${TARGET}.h

lib${TARGET}.a: ${TARGET}.o
	${AR} ${ARFLAGS} $@ $<

${TARGET}: main.o lib${TARGET}.a
	${CC} ${CFLAGS} $^ -o $@
	-strip ${TARGET}

${TARGET}.1: readme.md
	pandoc -s -f markdown -t man $< -o $@

install: ${TARGET} lib${TARGET}.a ${TARGET}.1 .git
	install -p -D ${TARGET} ${DESTDIR}/bin/${TARGET}
	install -p -m 644 -D lib${TARGET}.a ${DESTDIR}/lib/lib${TARGET}.a
	install -p -m 644 -D ${TARGET}.h ${DESTDIR}/include/${TARGET}.h
	-install -p -m 644 -D ${TARGET}.1 ${DESTDIR}/man/${TARGET}.1
	mkdir -p ${DESTDIR}/src
	cp -a .git ${DESTDIR}/src
	cd ${DESTDIR}/src && git reset --hard HEAD

dist: install
	tar zcf ${TARGET}-${VERSION}.tgz ${DESTDIR}

check:
	-scan-build make
	-cppcheck --enable=all *.c

clean:
	rm -fv ${TARGET} *.o *.a *.tgz *.1
	-git clean -dffx

small: CFLAGS=-std=c99 -Os -m32 -DNDEBUG -Wall -Wextra -fwrapv -DPICKLE_VERSION="${VERSION}"
small: main.c ${TARGET}.c ${TARGET}.h
	${CC} ${CFLAGS} main.c ${TARGET}.c -o $@
	-strip $@

micro: CFLAGS=-DNDEBUG -DDEFINE_TESTS=0 -DDEFINE_MATHS=0 -DDEFINE_STRING=0 -DDEFINE_REGEX=0 -DDEFINE_LIST=0 -DPICKLE_VERSION="${VERSION}"
micro: CFLAGS+=-std=c99 -Os -m32 ${DEFINES} -Wall -Wextra -fwrapv
micro: main.c ${TARGET}.c ${TARGET}.h
	${CC} ${CFLAGS} main.c ${TARGET}.c -o $@
	-strip $@

fast: CFLAGS=-std=c99 -O3 -DNDEBUG -static -Wall -Wextra -DPICKLE_VERSION="${VERSION}"
fast: main.c ${TARGET}.c ${TARGET}.h
	${CC} ${CFLAGS} main.c ${TARGET}.c -o $@
	-strip $@

debug: CFLAGS=-std=c99 -g -Wall -Wextra -DPICKLE_VERSION="${VERSION}"
debug: main.c ${TARGET}.c ${TARGET}.h shell
	${CC} ${CFLAGS} main.c ${TARGET}.c -o $@

profile: debug shell
	valgrind --tool=callgrind ./debug shell -t
	#kcachegrind

#tags:
#	ctags -R -f ./.git/tags .

