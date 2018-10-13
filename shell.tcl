#!./pickle
# Simple Shell
# Before this is usable, several things need to be done:
# - Evaluation could be done in a sandbox
# - ARGC/ARGC could be processed

set HOME "HOME"
set OS Unix
if {eq [getenv OS] "Windows_NT" } {
	set OS Windows
	set HOME "HOMEPATH"
}

set HOME [getenv $HOME]
puts $HOME

source $HOME/.picklerc

set colorize [getenv COLOR]
proc color {x} { 
	upvar #0 colorize c; 
	if {eq $c on} { return $x } else { return "" } 
}

proc normal {} { color "\x1b\[0m" }
proc red    {} { color "\x1b\[31;1m" }
proc green  {} { color "\x1b\[32;1m" }
proc blue   {} { color "\x1b\[34;1m" }

proc incr {x} { upvar 1 $x i; set i [+ $i 1] }

# Hold over from Forth
proc words {} {
	set i 0
	set m [command]
	while {< $i $m} {
		puts -nonewline "[command name $i] "
		incr i
	}
	puts ""
}

# puts "Commands defined:"
# words

puts "For help, type 'help', for a list of commands type 'words'"
puts "To quit, type 'exit', or press CTRL+D on a Unix (or CTRL-Z in Windows)"

puts -nonewline $prompt
set line [gets]

while {ne $line ""} {
	set result [catch {eval $line} retcode]
	set fail [red]
	if {== $retcode 0} { set fail [green] }
	puts "\[$fail$retcode[normal]\] $result"
	puts -nonewline $prompt
	set line [gets]
}

