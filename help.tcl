#!./pickle
# This is the help file for Pickle as a program. We will not define
# any functions within this file so as not to clash with TCL instances
# that source this file.

# TODO Remove help command in 'main.c', expand this help.

set more {puts -nonewline "-- more --"; gets; }
puts {}
puts {Pickle:     A tiny TCL like language derived/copied from 'picol'}
puts {License:    BSD (Antirez for original picol, Richard Howe for modifications)}
puts {Repository: https://github.com/howerj/pickle}
puts "Version:    $version"
puts {
Pickle is a tiny TCL language, like the original TCL, it is designed to be
embedded within other programming environments. It excels as a glue language,
for processing strings, and as a command language.
}
# eval $more
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
A list of defined commands in no particular order:
}

set i 0
set m [command]
while {< $i $m} {
	puts -nonewline "[command name $i] "
	set i [+ $i 1]
}
puts ""

puts {
EOF
}
