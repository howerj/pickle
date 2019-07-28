#!./pickle -a
# This is the help file for Pickle as a program. We will not define
# any functions within this file so as not to clash with TCL instances
# that source this file.

set more {puts -nonewline "-- more --"; gets; }
puts {}
puts {Pickle:     A tiny TCL like language derived/copied from 'picol'}
puts {License:    BSD (Antirez for original picol, Richard Howe for modifications)}
puts {Repository: https://github.com/howerj/pickle}
puts "Version:    [uplevel #0 set version]"
puts {
Pickle is a tiny TCL language, like the original TCL, it is designed to be
embedded within other programming environments. It excels as a glue language,
for processing strings, and as a command language.
}
eval $more
puts {
The language itself is compact and easy to understand, but only if you take the
time to understand the language. If you approach the language as another
language with C like syntax you will leave disappointed and confused. It has
more in common with LISP than C. The library itself does interface very well
with C however, and is very easy to extend from within C.
}
puts {
There are only a few concepts needed understand Pickle; about five syntax
rules, the fact that everything is a string (like in LISP, except in LISP
everything is a list), and that each statement is a command (a Pickle program
is a sequence of commands which are executed).
}

puts {
	# Comments are commands (sort of) that begin with '#'
	# Comments cannot be on the same line as a command!
	+ 2 2
	- -3 4
	+ [* 3 3] [* 4 4]
	set result [* 3 4]
}

puts {
'if' is a command as well, it accepts a condition which it evaluates, and
depending on the result evaluates one of two clauses.

	if {== 2 3} {
		puts "Something is very wrong"
	} else {
		puts "All is okay."
	}
}

eval $more
puts {
We can also declare variables with 'set', setting a variable to a string. We
can refer to this variable with a '$':

	set a 3;
	puts "a is equal to $a";
}

puts {
Functions can be defined, they are placed in a different
namespace than variables are:

	proc square {x} { * $x $x }

	puts "square of 4 is [square 4]"
}

puts {
A function example with looping:

	proc example {x} {
		set a 0
		while {< $a $x} {
			puts "a = $a"
			set a [+ $a 1]
		}
	}
	example 4
}

puts {
The command 'gets' is used to retrieve a line of input text:

	puts "What is your name?";
	set name [gets];
	puts "Hello, $name"
}

eval $more
puts {
A list of defined commands in no particular order:
}

set i 0
set m [info command]
set l 0
while {< $i $m} {
	set n [info command name $i]
	set l [+ $l [string length $n]]
	set c " "
	if {> $l 72} { set c "\n"; set l 0 }
	puts -nonewline "$n$c"
	set i [+ $i 1]
}
puts ""

puts {
EOF
}
unset more
