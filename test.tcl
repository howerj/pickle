#!/bin/pickle
# This file contains unit tests for the Pickle Interpreter
# TODO:
# - implement full test suite
# - implement ANSI Terminal Color Codes

proc die {x} {
	puts $x
	exit 1
}

set passed 0
set total  0

proc red   {} { return "\x1b\[31;1m" }
proc green {} { return "\x1b\[32;1m" }
proc blue  {} { return "\x1b\[34;1m" }

# puts [blue]

proc assert {x} {
	if {== $x "0"} {
		die "assert failed"
	}
}

proc incr {x} {
	upvar 1 $x i
	set i [+ $i 1]
}

proc decr {x} {
	upvar 1 $x i
	set i [- $i 1]
}

proc test {x} {
	set r [eval $x]
	upvar #0 total t
	incr t
	if {!= $r "0"} {
		uplevel #0 { set passed [+ $passed 1] }
		set f "ok:   "
	} else {
		set f "FAIL: "
	}
	puts "$f$x = $r"
	unset t
}

proc square {x} { * $x $x }

puts "Pickle Unit Tests"

test {== 2 2}
test "== 16 \[square 4\]"
test "== 3  \[length 123\]"
test {eq a a}
test "eq \"a b\" \[concat a b\]"

test {+ 2 2}
puts ""

assert [<= $passed $total]

# TODO: Implement escape characters correctly
puts "pass/total $passed/$total"
puts "\[DONE\]"

