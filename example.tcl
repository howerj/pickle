#!./pickle
#
# These are toy programs to test the interpreter. The 'loop forever'
# and the 'performance test' programs have some minor utility.
#

set program [argv 2]
set argc [argv]

proc usage {} {
	puts "Usage: example.tcl -\[123\]"
	puts ""
	puts "\t-1\tSimple nonsense test program"
	puts "\t-2\tLoop forever printing '.'"
	puts "\t-3\tPerformance test"
	puts ""
	bye
}

if {== $argc 0} { usage }

if {eq $program "-1"} {
	proc square {x} {
		* $x $x
	}

	set a 1
	while {<= $a 10} {
		if {== $a 5} {
			puts {Missing five!}
			set a [+ $a 1]
			continue
		}
		puts "I can compute that $a*$a = [square $a]"
		set a [+ $a 1]
	}
	bye
}

if {eq $program "-2"} {
	proc forever {} { while {== 1 1} { puts "." } }
	forever
}

# Crude Performance Test
#
# Using 'incr loop -1' greatly speeds things up, but
# exercises less.
#
if {eq $program "-3" } {
	proc decr {x} { upvar 1 $x i; set i [- $i 1] }

	proc waste {x} {
		set loop $x;
		while {!= 0 $loop} {
			decr loop
		}
	}

	# Takes about a second on my machine
	waste 200000;
	puts [clock];
	bye
}

usage


