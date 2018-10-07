CFLAGS=-std=c99 -Wall -Wextra -pedantic -O2 -g
# CFLAGS=-std=c99 -Wall -Wextra -pedantic -Os -fwrapv -Wl,--gc-sections -ffunction-sections -fdata-sections -DNDEBUG

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

pickle: pickle.o main.o block.o
	${CC} ${CFLAGS} $^ -o $@
	# strip $@

clean:
	rm -rf picol pickle *.o *.a
