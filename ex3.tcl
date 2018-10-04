
proc assert {x} {
	if {eq $x ""} {
		exit -1
	}
}


puts [match a*b aaab]
puts [match a*b b]

