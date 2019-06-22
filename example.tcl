#!./pickle
#
# PICKLE EXAMPLE PROGRAMS
#

set program [argv 2]

puts $program

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

# Performance Test
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

puts "Usage: [argv 0] -\[123\]"

bye
