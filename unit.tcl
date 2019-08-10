#!./pickle -a
#
# This file contains unit tests for the Pickle Interpreter. The things tested
# in this file are mainly the commands and sub-commands that perform simple
# functions, and not basic language constructions (and commands like 'if',
# or 'while') as we would have to use those constructs as part of the test
# framework itself. Instead, those things should be considered tested if
# the test framework itself runs.
#
# TODO: Find a way to merge the 'shell.tcl' application
# TODO: Test parse/recursion failure, other failure cases.
# TODO: Define various functions if they are not; 'ne', 'eq', 'incr'
# TODO: Being able to run this test suite in the 'simple' version of the
# interpreter would allow the removal of many commands written in C. It may
# require some extensions to the simple version of the interpreter (gets, puts,
# exit, getenv, and reading from a file if one has been given).
#

set program [argv 2]
set argc [argv]

proc usage {} {
	puts "Usage: [argv 0] -\[pht\]"
	puts ""
	puts "\t-h\tPrint this help message and quit"
	puts "\t-p\tPerformance test"
	puts ""
	bye
}

# if {> $argc 2} { usage }
if {eq $program "-h" } { usage }

# Crude Performance Test
#
# Using 'incr loop -1' greatly speeds things up, but
# exercises less.
#
if {eq $program "-p" } {
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

# ### Clean up   ### #

rename usage ""
unset program
unset argc

# ### Unit Tests ### #

proc die {x} { puts $x; exit 1 }

set passed 0
set total  0

proc assert {x} {
	if {== $x "0"} {
		die "assert failed"
	}
}

# Set environment variable COLOR to 'on' to turn on color
set colorize [getenv COLOR]
proc color {x} {
	upvar #0 colorize c;
	if {eq $c on} { return $x } else { return "" }
}

proc normal {} { color "\x1b\[0m" }
proc red    {} { color "\x1b\[31;1m" }
proc green  {} { color "\x1b\[32;1m" }
proc blue   {} { color "\x1b\[34;1m" }

proc defined {x} {
	set i 0
	set m [info command]
	set r undefined
	while {< $i $m} {
		if {eq $x [info command name $i]} { return command }
		incr i
	}
	catch {uplevel 1 "set $x"} e
	if {eq 0 $e} { return variable }
	return $r
}

# BUG: If the unit test sets the value of 'x' then it can mess
# up printing the error.
proc test {result x} {
	set retcode 0
	set r [catch $x retcode]
	upvar #0 total t
	incr t

	if {and [eq $r $result] [eq 0 $retcode]} {
		uplevel #0 { set passed [+ $passed 1] }
		set f "[green]ok[normal]:    "
	} else {
		set f "[red]FAIL[normal]:  (expected \"$result\") "
	}
	puts "$f{$x} = \"$r\""
}

# Test failure cases
proc fails {x} {
	set retcode 0
	set r [catch $x retcode]
	upvar #0 total t
	incr t
	if {== $retcode -1} {
		uplevel #0 { set passed [+ $passed 1] }
		set f "[green]ok[normal]:    "
	} else {
		set f "[red]FAIL[normal]:  "
	}
	puts [string tr r "\n\r" " " "$f{$x} with error \"$r\""]
}

proc state {x} {
		puts "[blue]state[normal]: $x"
		eval $x
}

puts "SYSTEM OPTIONS"
puts "Pointer Size: [info sizeof pointer]"
puts "Number  Size: [info sizeof number]"
puts "Limits: "
puts "\trecursion:  [info limits recursion]"
puts "\tstring:     [info limits string]"
puts "\tminimum:    [info limits minimum]"
puts "\tmaximum:    [info limits maximum]"
puts "Features:"
puts "\tallocator:  [info features allocator]"
puts "\tstring:     [info features string]"
puts "\tmaths:      [info features maths]"
puts "\tdebugging:  [info features debugging]"
puts "\tstrict:     [info features strict]"
puts "\tstr-length: [info features string-length]"


puts "\[Pickle Tests\]"

test hello {if {bool 1} { concat "hello" }}
test 1 {bool 4}
test 0 {bool 0}
fails {bool}
fails {$a}

if {== 0 [info features strict]} {
	test 0 {bool 0b1}
	test 1 {bool 9b1}
	test 1 {== 0 abc}
	test 1 {!= 0 1abc}
	test 1 {not x}
}

fails {set}
fails {unset}
fails {if}
# NB. Different from TCL, which is an error
test {1} {if { } { return 1 0 } else { return 2 0 }}
fails {if {== 0 0} { puts a } not-else { puts b }}
fails {while}
fails {while {== 0 x} { puts "not run" }}
state {proc recurse {} { recurse }}
fails {recurse}
state {rename recurse ""}
fails {[}
fails {]}
test 1 {== 2 2}
test 1 {!= 0 2}
test 1 {== -0 0}
test 0 {== -1 1}
test 4 {+ 2 2}
test -4 {+ 2 -6}
fails {+ 2 a}
fails {+ 2}
test -4 {- 2 6}
test 16 {* -2 -8}
test 1 "< 3 4"
test 0 {< 5 -5}
test -25 {* 5 -5}
test 1 {< 6  9}
test 0 {> 6  9}
test 0 {> -6  9}
test 1 {>= -6  -6}
test 1 {>= 6  -6}
test 0 {>= -6  6}
test 4 {lshift 1 2}
test 5 {rshift 10 1}
test 9 {min 90 9}
test -9 {min 90 -9}
test -4 {max -5 -4}
test 4 {abs 4}
test 4 {abs -4}
test -1 {+ [invert 1] 1}
test 255 {or 85 170}
test 255 {xor 85 170}
test 0   {and 85 170}
test 0 {not 3}
test 1 {not 0}
test 3 {/ 12 4}
fails {/ 1 0}
fails {/ 0 0}
test 4 {mod 4 12}
fails {mod 4 0}
test 11 {mod 11 12}
test 0 {mod 12 12}
test 1 {mod 13 12}
test -3 {negate 3}
test 3 {negate -3}
test 120 {set cnt 5; set acc 1; while {> $cnt 1} { set acc [* $acc $cnt]; incr cnt -1 }; set acc; };
test 1 {eq a a}
fails {eq}
fails {eq a}
test 0 {eq a b}
test 1 {ne abc ""}
test 1 {eq "" ""}
test 1 {eq "a b" [concat a b]}
test {a b c d e f {g h}} {concat a b "c d e  " "  f {g h}"}
test a,b,c {join {a b c} ,}
fails {join}
fails {join {}}
state {proc square {x} { * $x $x }}
test 16 {square 4}
fails {square a}
state {rename square sq}
state {proc fib {x} { if {<= $x 1} { return 1; } else { + [fib [- $x 1]] [fib [- $x 2]]; } }}
test 89 {fib 10}
test 0 {> 0 [info command fib]}
state {rename fib ""}
test -1 {info command fib}
test 16 {sq 4}
state {rename sq ""}
fails {string}
test 3 {string length 123}
test 4 {string length 1234}
test 4 {string length abcd}
test 0 {string length ""}
test 0 {string length {}}
fails {string length}
test 1 {string match %% %}
test 1 {string match "" ""}
test 1 {string match "*" ""}
test 1 {string match "*" "a"}
test 1 {string match "*" "aaa"}
test 0 {string match "%*" ""}
test 0 {string match "%*" "a"}
test 0 {string match "%*" "aaa"}
test 1 {string match "?" "?"}
test 1 {string match "?" "a"}
test 1 {string match "%?" "?"}
test 1 {string match "???" "abc"}
test 1 {string match "???" "abc"}
test 0 {string match "???" "abcd"}
test 0 {string match "abc" "abcd"}
test 1 {string match "abc" "abc"}
test 1 {string match "abc*" "abc"}
test 0 {string match "abc*d" "abc"}
test 1 {string match "abc*d" "abcd"}
test 1 {string match "abc*d" "abcXXXXd"}
test 1 {string match "*" "ahoy!"}
test 1 {string match "*abc*c?d" "xxxxxabcxxxxc3d"}
test 1 {string match "*abc*c?d" "xxxxxabcxxxxc?d"}
fails {string match}
fails {string match ""}
test ""     {string reverse ""}
test "a"    {string reverse "a"}
test "ba"   {string reverse "ab"}
test "cba"  {string reverse "abc"}
test "dcba" {string reverse "abcd"}
test "ba0"  {string reverse "\x30ab"}
fails {string reverse}
test ""        {string trimleft ""}
test ""        {string trimleft "   "}
test "x "      {string trimleft "  x "}
test "x b"     {string trimleft "  x b"}
test "123ABC." {string toupper "123aBc."}
test "123abc." {string tolower "123aBc."}
fails {string tolower}
test 0 {string equal a b}
test 1 {string equal a a}
test 1 {< 0 [string compare a A]}
test 1 {> 0 [string compare bA ba]}
test 0 {string compare-no-case a a}
test 0 {string compare-no-case a A}
test 0 {string compare-no-case B b}
test 0 {string compare-no-case abc ABC}
test 1 {> 0 [string compare-no-case a B]}
test 1 {> 0 [string compare-no-case A b]}
test h {string index hello 0}
test e {string index hello 1}
test l {string index hello 3}
test o {string index hello 4}
test o {string index hello 9}
test o {string index hello -1}
test l {string index hello -2}
test h {string index hello -9}
fails {string index hello x}
fails {string index hello}
fails {string index}
test "" {string repeat abc 0}
test "abc" {string repeat abc 1}
test "abcabc" {string repeat abc 2}
test "aaa" {string repeat a 3}
fails {string repeat}
fails {string repeat a}
fails {string repeat a -1}
fails {string repeat a a}
fails {string is}
test 1 {string is digit ""}
test 1 {string is digit 1234567890}
test 0 {string is digit 123a}
test 1 {string is alnum ""}
test 1 {string is alnum 123a}
test 0 {string is alnum 123.}
test 0 {string is alpha 123.}
test 0 {string is alpha 123a.}
test 1 {string is alpha abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ}
test 1 {string is ascii ""}
test 0 {string is ascii "a\x80a"}
test 0 {string is ascii "\x82"}
test 1 {string is ascii "\x01"}
test 1 {string is ascii "\x1 "}
test 1 {string is ascii "\x1"}
test 1 {string is ascii "aa"}
test 1 {string is ascii "\x7f"}
test 1 {string is xdigit ""}
test 1 {string is xdigit "1234567890ABCDEFabcdef"}
test 0 {string is xdigit "1234567890ABCDEFabcdefQ"}
test 0 {string is lower "aB"}
test 1 {string is lower "abcdefghijklmnopqrstuvwxyz"}
test 0 {string is lower "abcdefghijklmnopqrstuvwxyz "}
test 0 {string is lower "aa123"}
test 1 {string is lower ""}
test 0 {string is upper "aB"}
test 1 {string is upper "ABCDEFGHIJKLMNOPQRSTUVWXYZ"}
test 0 {string is upper "ABCDEFGHIJKLMNOPQRSTUVWXYZ "}
test 0 {string is upper "AA123"}
test 1 {string is upper ""}
test 1 {string is wordchar ""}
test 1 {string is wordchar "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345789abcdefghijklmnopqrstuvwxyz_"}
test 0 {string is wordchar " ABCDEFGHIJKLMNOPQRSTUVWXYZ012345789abcdefghijklmnopqrstuvwxyz_"}
test 0 {string is wordchar " "}
test 0 {string is wordchar ":"}
test 0 {string is integer ""}
test 0 {string is integer " 0"}
test 0 {string is integer "123a"}
test 0 {string is integer " 123"}
test 0 {string is integer "0x123"}
test 0 {string is integer "++0"}
test 0 {string is integer "-"}
test 0 {string is integer "+"}
test 1 {string is integer "0"}
test 1 {string is integer "12345"}
test 1 {string is integer "67890"}
test 1 {string is integer "-123"}
test 1 {string is integer "+123"}
test 1 {string is integer "0123"}
test 1 {string is integer "+0"}
test 1 {string is integer "-0"}
test 1 {string is boolean false}
test 1 {string is boolean no}
test 1 {string is boolean 0}
test 1 {string is boolean OFF}
test 1 {string is boolean true}
test 1 {string is boolean yes}
test 1 {string is boolean 1}
test 1 {string is boolean on}
test 0 {string is boolean XXX}
test 0 {string is boolean ""}
test 1 {string is true true}
test 1 {string is true yEs}
test 1 {string is true 1}
test 1 {string is true oN}
test 0 {string is true off}
test 0 {string is false oN}
test 3 {string first a bbbaca}
test 3 {string first a bbbaca 3}
test 5 {string first a bbbaca 4}
test -1 {string first d bbbaca}
test 0 {string ordinal ""}
test 0 {string ordinal "\x00"}
test 1 {string ordinal "\x01"}
test 2 {string ordinal "\x2"}
test 48 {string ordinal "0"}
test 49 {string ordinal "1"}
test 49 {string ordinal "1a"}
test 49 {string ordinal "1abc"}
fails {string ordinal }
test ff {string dec2hex 255}
test 1000 {string dec2hex 4096}
test 4096 {string hex2dec 1000}
if {== [info sizeof number] 16} { test -1 {string hex2dec FffF} } else { test 65535 {string hex2dec FffF} }
test 101 {string dec2base 5 2}
fails {string dec2base A 2}
test 5 {string base2dec 101 2}
fails {string base2dec 0 0}
fails {string base2dec 0 1}
fails {string base2dec 0 37}
fails {string base2dec 2 2}
fails {string base2dec 2}
test 0 {string char 48}
test 1 {string char 49}
test a {string char 97}
test {} {string tr d abc ""}
test {ddeeff} {string tr d abc aabbccddeeff}
test {aabbcc} {string tr dc abc aabbccddeeff}
test {ddeeffddeeff} {string tr r abc def aabbccddeeff}
test {defdefgg} {string tr s abc def aabbccddeeffgg}
test {abcddeeffgg} {string tr s abc aabbccddeeffgg}
fails {string tr}
fails {string tr d}
test {ad} {string replace "abcd" 1 2 ""}
test {acd} {string replace "abcd" 1 1 ""}
test {abcd} {string replace "abcd" 2 1 ""}
test {a123d} {string replace "abcd" 1 2 "123"}
test {a1d} {string replace "abcd" 1 2 "1"}
test {123} {string replace "" 0 0 "123"}
test {hello world} {set tv1 hello; subst {$tv1 world}}
test 0 {llength ""}
test 0 {llength " "}
test 1 {llength "a"}
test 2 {llength "a b"}
test 2 {llength "a b "}
test 2 {llength "a { b }"}
test 3 {llength "a { b } c"}
test 3 {llength "a { { b } } c"}
test 3 {llength {a { { \" b \" } } c}}
test 3 {llength {a " b " c}}
fails {llength}
test a {lindex "a b c" 0}
test b {lindex "a b c" 1}
test c {lindex "a b c" 2}
test c {lindex "a b c" 2}
# Note this is different to TCL, where it is an error
test 3 {llength "a { b }c"}
test { b } {lindex "a { b } c" 1}
test a {lindex "a { b } c" 0}
test c {lindex "a { b } c" 2}
test "" {lindex "a { b } c" 3}
test {$x} {subst -novariables {$x}}
test {$x 3} {subst -novariables {$x [+ 2 1]}}
fails {subst}
test {a 3} {subst {a [+ 2 1]}}
test {a [+ 2 1]} {subst -nocommands {a [+ 2 1]}}
test {a hello c} {set z "a b c"; lset z 1 hello}
test {a hello} {set z "a b"; lset z 1 hello}
test {a c} {set z "a b c";   lset z 1 ""}
test {a} {set z "a b";   lset z 1 ""}
test {} {set z "a";   lset z 0 ""}
test {b} {set z "a b";   lset z 0 ""}
test {a b c} {split a.b.c .}
test {a { } b { } c} {split "a b c" ""}
test {a b} {split "a.b" "."}
test {{a b} c} {split "a b.c" "."}
test {abc d} {split "abc.d" "."}
test {a {}} {split "a." "."}
test {} {split "" ""}
test {{} {}} {split "." "."}
test {{} {} {}} {split ".." "."}
fails {split}
fails {split "abc"}
test {a b {c d e  } {  f {g h}}} {list a b "c d e  " "  f {g h}"}
test {a b c xyz} {linsert {a b c} 99 xyz}
test {a b c xyz} {linsert {a b c} 3 xyz}
test {xyz a b c} {linsert {a b c} 0 xyz}
test {a {x yz} b c} {linsert {a b c} 1 {x yz}}
test {} {lrepeat 0 a}
test {a} {lrepeat 1 a}
test {a a} {lrepeat 2 a}
test {a a a} {lrepeat 3 a}
test {ab ab ab} {lrepeat 3 ab}
test {{a b} {a b} {a b}} {lrepeat 3 {a b}}
test {a,b,c} {join {a b c} ,}
test {abc} {join [split "abc" ""] ""}
test {a,b,c} {conjoin , a b c}
test {a} {conjoin , a}
test {a,b} {conjoin , a b}
test {} {conjoin , ""}
fails {conjoin}
test {} {lreverse {}}
test {a} {lreverse {a}}
test {b a} {lreverse {a b}}
test {c b a} {lreverse {a  b c}}
test {{ c} a} {lreverse {a { c}}}
test {{y y} {x x}} {lreverse {{x x} {y y}}}
fails {lreverse}
fails {proc}
fails {proc x}
fails {proc x n}
state {proc def {x} {}}
fails {proc def {x} {}}
state {rename def ""}
fails {variadic}
state {variadic v1 n { return $n 0 }}
test {a b {c d}} {v1 a b {c d}}
test {a} {v1 a}
test {} {v1}
state {rename v1 ""}
fails {rename}
fails {rename XXX}
fails {rename XXX YYY}
test {} {lsort {}}
test {a} {lsort {a}}
test {a b} {lsort {a b}}
test {a b} {lsort {b a}}
test {a b c} {lsort {b a c}}
test {c b a} {lsort -decreasing {b a c}}
test {a b c} {lsort {a b c}}
test {1 2 3} {lsort -integer {1 2 3}}
test {3 2 1} {lsort -integer -decreasing {1 2 3}}
fails {lsort}
fails {lsort -integer {1 2 a}}

# Test upvar links
state {proc n2 {} { upvar 1 h u; set u [+ $u 1]; }}
state {proc n1 {} { upvar 1 u h; set h [+ $h 1]; n2 }}
test {done} {set u 5; puts "u = $u"; puts "[n1]"; test 1 "== $u 7"; unset u; return "done" 0; }
state {rename n1 ""}
state {rename n2 ""}

fails {lreplace}
fails {lreplace ""}
fails {lreplace "" 1}
test {a foo c d e} {lreplace {a b c d e} 1 1 foo}
test {a {x x} c d e} {lreplace {a b c d e} 1 1 {x x}}
test {a three more elements d e} {lreplace {a b c d e} 1 2 three more elements}
test {a b} {lrange {a b c d e} 0 1}
test {a b c d e} {lrange {a b c d e} 0 4}
test {a} {lrange {a b c d e} 0 0}
test {c d} {lrange {a b c d e} 2 3}
test {a b c d} {lrange {a b c d e} -2 3}
test 1 {lsearch {x abc def} abc}
test -1 {lsearch {x abc def} xyz}
test 0 {lsearch {x abc def} *x*}
test 2 {lsearch -exact {x abc *x*} *x*}
test 4 {lsearch -exact -start 2 {a a b c a} a}
test 2 {lsearch {4 5 6} 6}
test 4 {lsearch -not {a a a a b a a} a}
test bbbb {lsearch -not -inline {a aaa  aa bbbb aaa aa} a*}
fails {lsearch}
fails {lsearch {1 2 3}}
fails {lsearch -integer {1 2 3} x}

# # Fails for now
# test {x y z a b c} {lreplace {a b c} -1 -2 x y z}
# test {a b x y z c} {lreplace {a b c} 2 1 x y z}

assert [<= $passed $total]
assert [>= $passed 0]

set failed [!= $passed $total]
set emphasize [green]

if {!= $failed 0} {
	set emphasize [red]
}

puts "$emphasize   pass[normal]/[blue]total $emphasize$passed[normal]/[blue]$total[normal]"
puts "\[[blue]DONE[normal]\]"

if {== "[heap]" 0 } {
	puts "Custom allocator not used"
	# Causes memory leak to appear in Valgrind, not a real leak however.
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
	puts "ARENA $i:   $blk $sz $used $max"
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

