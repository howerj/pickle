#!/bin/pickle
# This file contains unit tests for the Pickle Interpreter

proc die {x} {
	puts $x
	exit 1
}

set passed 0
set total  0

proc assert {x} {
	if {== $x "0"} {
		die "assert failed"
	}
}

# TODO: Implement upvar/uplevel for this
proc test {x} {
	set total [+ $total 1]
	if {!= $x "0"} {
		set passed [+ $passed 1]
	}
}

proc square {x} { * $x $x }

puts "Pickle Unit Tests"

assert [== 2 2]
assert [== 16 [square 4]]
assert [== 3  [length 123]]

# TODO: Implement escape characters correctly
puts \[DONE\]

