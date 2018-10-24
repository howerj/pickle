#!./pickle -a
# This file contains unit tests for the Pickle Interpreter

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

proc test {result x} {
	set r [eval $x]
	upvar #0 total t
	incr t

	if {eq $r $result} {
		uplevel #0 { set passed [+ $passed 1] }
		set f "[green]ok[normal]:   "
	} else {
		set f "[red]FAIL[normal]: (expected $result) "
	}
	puts "$f$x = $r"
	unset t
}

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

test hello {if {bool 1} { concat "hello" }}
test 1 {bool 4}
test 0 {bool 0}
test 0 {bool 0b1}
test 1 {bool 9b1}
test 1 {== 2 2}
test 4 {+ 2 2}
test 1 "< 3 4"
test 16 {square 4}
test 3  {length 123}
test 4  {length 1234}
test 4  {length abcd}
test 0  {length ""}
test 1 {eq a a}
test 0 {eq a b}
test 1 {ne abc ""}
test 1 {eq "" ""}
test 1 {eq "a b" [concat a b]}
test 1 "eq \"a,b,c\" \[join , a b c\]"
test 1 {match %% %}
test 1 {match "%?" "?"}
test 1 {match "???" "abc"}
test 1 {match "???" "abc"}
test 0 {match "???" "abcd"}
test 0 {match "abc" "abcd"}
test 1 {match "abc" "abc"}
test 1 {match "abc*" "abc"}
test 0 {match "abc*d" "abc"}
test 1 {match "abc*d" "abcd"}
test 1 {match "abc*d" "abcXXXXd"}
test 1 {match "*" "ahoy!"}
test 1 {match "*abc*c?d" "xxxxxabcxxxxc3d"}
test 1 {match "*abc*c?d" "xxxxxabcxxxxc?d"}
test 89 {fib 10}
test 0 {< 5 -5}
test -25 {* 5 -5}
test 1 {< 6  9}
test 0 {> 6  9}
test 0 {> -6  9}
test 1 {>= -6  -6}
test 1 {>= 6  -6}
test 0 {>= -6  6}
test 4 {<< 1 2}
test 5 {>> 10 1}
test 9 {min 90 9}
test -9 {min 90 -9}
test -4 {max -5 -4}
test 4 {abs 4}
test 4 {abs -4}
test -1 {+ [~ 1] 1}
test 255 {| 85 170}
test 255 {^ 85 170}
test 0   {& 85 170}
test 0 {! 3}
test 1 {! 0}
test 1 {! x}
test 3 {/ 12 4}
test 120 {set cnt 5; set acc 1; while {> $cnt 1} { set acc [* $acc $cnt]; decr cnt }; set acc; };

# Test upvar links
set u 5
puts "u = $u"
puts "[n1]"
test 1 "== $u 7"
unset u

puts "[n1]"
test 1 "== $u 2"
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
	set blk   [heap arena-block  $i]
	set sz    [heap arena-size   $i]
	set used  [heap arena-active $i]
	set max   [heap arena-max    $i]
	set m [+ $m [* $blk $sz]]
	puts "ARENA($i):   $blk $sz $used $max"
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

