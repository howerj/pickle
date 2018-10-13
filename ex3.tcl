#!./pickle
# Performance Test

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

