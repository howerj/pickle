#!/bin/pickle
# This file contains unit tests for the Pickle Interpreter

proc die {x} {
	puts $x
	exit 1
}

proc assert {x} {
	if {== $x "0"} {
		die "assert failed";
	}
}

set passed 0
set total  0

puts "Pickle Unit Tests"

assert [== 2 2]

puts "All Done"
