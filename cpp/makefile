CXX=g++
CXXFLAGS=-Wall -Wextra -pedantic -O2 -std=c++11 -I..

.PHONY: clean test

cpp: main.cpp pickle.hpp libpickle.a
	${CXX} ${CXXFLAGS} $< libpickle.a -o $@

test: cpp
	./cpp

libpickle.a:
	make -C .. libpickle.a
	cp ../libpickle.a .

clean:
	rm -vf cpp libpickle.a

