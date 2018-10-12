#!./pickle
# This file contains unit tests for the Pickle Interpreter
# TODO:
# - implement full test suite
# - implement ANSI Terminal Color Codes

proc die {x} { puts $x; exit 1 }

set passed 0
set total  0

proc assert {x} {
	if {== $x "0"} {
		die "assert failed"
	}
}

proc incr {x} { upvar 1 $x i; set i [+ $i 1] }
proc decr {x} { upvar 1 $x i; set i [- $i 1] }

# Set environment variable COLOR to 'on' to turn on color
set colorize [getenv COLOR]
proc color {x} { 
	upvar #0 colorize c; 
	if {eq $c on } { return $x } else { return "" } 
}

proc normal {} { color "\x1b\[0m" }
proc red    {} { color "\x1b\[31;1m" }
proc green  {} { color "\x1b\[32;1m" }
proc blue   {} { color "\x1b\[34;1m" }

proc test {x} {
	set r [eval $x]
	upvar #0 total t
	incr t

	if {!= $r "0"} {
		uplevel #0 { set passed [+ $passed 1] }
		set f "[green]ok[normal]:   "
	} else {
		set f "[red]FAIL[normal]: "
	}
	puts "$f$x = $r"
	unset t
}

# proc waste {x} { set loop $x; while {!= 0 $loop} { decr loop } }
# puts [clock]; waste 999999; puts [clock]; 

proc square {x} { * $x $x }

proc fib {x} {
	if {<= $x 1} {
		return 1
	} else {
		+ [fib [- $x 1]] [fib [- $x 2]]
	}
}

proc n2 {} { upvar 1 h u; set u [+ $u 1]; }
proc n1 {} { upvar 1 u h; set h [+ $h 1]; n2 }

puts "\[Pickle Tests\]"

test {== 2 2}
test {+ 2 2}
test "< 3 4"
test {== 16 [square 4]}
test {== 3  [length 123]}
test {eq a a}
test {eq "a b" [concat a b]}
test "eq \"a,b,c\" \[join , a b c\]"
test {match %% %}
test {match "%?" "?"}
test {match "???" "abc"}
test {match "???" "abc"}
test {== 0 {match "???" "abcd"}}
test {== 0 {match "abc" "abcd"}}
test {match "abc" "abc"}
test {match "abc*" "abc"}
test {== 0 {match "abc*d" "abc"}}
test {match "abc*d" "abcd"}
test {match "abc*d" "abcXXXXd"}
test {match "*" "ahoy!"}
test {match "*abc*c?d" "xxxxxabcxxxxc3d"}
test {match "*abc*c?d" "xxxxxabcxxxxc?d"}
test {== 89 [fib 10]}
set u 5
puts "u = $u"
puts "[n1]"
test "== $u 7"
unset u

puts "[n1]"
test "== $u 2"
unset u

assert [<= $passed $total]

set failed [!= $passed $total]
set emphasize [green]


if {!= $failed 0} {
	set emphasize [red]
}


puts "$emphasize   pass[normal]/[blue]total $emphasize$passed[normal]/[blue]$total[normal]"
puts "\[[blue]DONE[normal]\]"

if {== "[heap]" 0 } {
	puts "Custom allocator not used"
	# We should not really exit here as it prevents cleanup
	exit $failed
}

puts "MEMORY STATISTICS"

set heaps [heap arenas]
set m 0
set i 0

while {< $i $heaps} { 
	set blk   [heap arena-block $i]
	set sz    [heap arena-size $i]
	set used  [heap arena-used $i]
	set m [+ $m [* $blk $sz]]
	puts "ARENA($i):   $blk $sz $used"
	incr i
}

puts "TOTAL:      [heap total]"
puts "BLOCK:      [heap blocks]"
puts "MAX:        [heap max]"
puts "ACTIVE:     [heap active]"
puts "FREED:      [heap freed]"
puts "ALLOC:      [heap allocs]"
puts ""
puts "MEMORY:     $m"
puts "EFFICIENCY: [/ [* [heap total] 100] [heap blocks]]%"
puts "WASTED:     [- 100 [/ [* [heap max] 100] $m]]%"

unset heaps; unset i; unset m; unset blk; unset sz; unset used;

# Prints wrong line number on Windows, related (depends on this line!)
puts "line: [info line]"

# exit $failed
return "" $failed


