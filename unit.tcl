#!./pickle
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

# TODO Turn Color On/Off
proc normal {} { return "\x1b\[0m" }
proc red    {} { return "\x1b\[31;1m" }
proc green  {} { return "\x1b\[32;1m" }
proc blue   {} { return "\x1b\[34;1m" }

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

puts "\[Pickle Tests\]"

test {== 2 2}
test {+ 2 2}
test "< 3 4"
test "== 16 \[square 4\]"
test "== 3  \[length 123\]"
test {eq a a}
test "eq \"a b\" \[concat a b\]"
test "eq \"a,b,c\" \[join , a b c\]"


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


# exit $failed
return $failed ""

