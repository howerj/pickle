#!./pickle -a
# Simple Shell

set HOME "HOME"
set OS Unix
if {eq [getenv OS] "Windows_NT" } {
	set OS Windows
	set HOME "HOMEPATH"
}

proc decode {r} {
	incr r
	lindex  {error ok return break continue} $r
}

set HOME [getenv $HOME]
set initrc "$HOME/.picklerc"

set sourced 0
set status [catch {source $initrc} sourced]
set status [string trim $status]
# puts "st: $status"
if {== 0 $sourced} { puts "$status" }
unset status
unset initrc

set colorize [getenv COLOR]
proc color {x} {
	upvar #0 colorize c;
	if {string compare-no-case $c on} { return "" } else { return $x }
}

proc normal {} { color "\x1b\[0m" }
proc red    {} { color "\x1b\[31;1m" }
proc green  {} { color "\x1b\[32;1m" }
proc blue   {} { color "\x1b\[34;1m" }

# proc incr {x} { upvar 1 $x i; set i [+ $i 1] }

# Hold over from Forth; list defined commands
# TODO: Not sure if this works correctly and finds all values in the 
# dictionary. This function needs checking.
proc words {} {
	set i 0
	set m [info command]
	set l 0
	while {< $i $m} {
		set n [info command name $i]
		set l [+ $l [string length $n]]
		set c " "
		if {> $l 80} { set c "\n"; set l 0 }
		puts -nonewline "$n$c"
		incr i
	}
	puts ""
}

# Is a variable defined, and if so, is it a command or a variable?
proc defined {x} {
	set i 0
	set m [info command]
	set r {0 undefined}
	while {< $i $m} {
		if {eq $x [info command name $i]} { return {1 command} }
		incr i
	}
	catch {uplevel 1 "set $x"} e
	if {eq 0 $e} { return {2 variable} }
	return $r
}

proc help {} { source help.tcl }

# Decompiler, of sorts. The name 'see' comes from Forth, like the function
# 'words.
proc see {w} {
	if {eq [uplevel 1 "defined {$w}"] {2 variable}} {
		puts "set $w [uplevel 1 "set $w"]"
		return
	}
	set widx [info command $w]
	if {< $widx 0} {
		return "'$w' not defined" 1
	}

	set args [info command args $widx]
	set body [info command body $widx]
	set name [info command name $widx]
	puts "proc $name {$args} {$body}"
}

# puts "Commands defined:"
# words

puts "For help, type 'help', for a list of commands type 'words'"
puts "To quit, type 'exit', or press CTRL+D on a Unix (or CTRL-Z in Windows)"

proc io {} {
	upvar #0 prompt p
	upvar #0 line l
	puts -nonewline $p
	set e -1
	set l [catch {gets} e]
	if {| [eq [decode $e] break] [eq [decode $e] error]} {
		if {eq $l EOF} {
			exit 0
		}
		exit 1
	}
}

io

while {ne $line ""} {
	set result [catch {eval $line} retcode]
	set fail [red]
	if {== $retcode 0} { set fail [green] }
	puts "\[$fail$retcode[normal]\] $result"
	io
}

