#!./pickle
# Simple Shell
# Before this is usable, several things need to be done:
# - A file needs to be sourced
# - Evaluation could be done in a sandbox
# - puts needs to optionally put a new line

set colorize [getenv COLOR]
proc color {x} { 
	upvar #0 colorize c; 
	if {eq $c on } { return $x } else { return "" } 
}

proc normal {} { color "\x1b\[0m" }
proc red    {} { color "\x1b\[31;1m" }
proc green  {} { color "\x1b\[32;1m" }
proc blue   {} { color "\x1b\[34;1m" }

puts $prompt
set line [gets]

while {ne $line ""} {
	set result [catch {eval $line} retcode]
	set fail [red]
	if {== $retcode 0} { set fail [green] }
	puts "\[$fail$retcode[normal]\] $result"
	puts $prompt
	set line [gets]
}

