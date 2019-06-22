# pickle: A tiny TCL like interpreter

| Project   | Pickle: A tiny TCL like interpreter        |
| --------- | ------------------------------------------ |
| Author    | Salvator Sanfilippo (Original Interpreter) |
| Author    | Richard James Howe (Modifications Only)    |
| Copyright | 2007-2016 Salvatore Sanfilippo             |
| Copyright | 2018 Richard James Howe                    |
| License   | BSD                                        |
| Email     | howe.r.j.89@gmail.com                      |
| Website   | <https://github.com/howerj/pickle>         |


	   ▄███████▄  ▄█   ▄████████    ▄█   ▄█▄  ▄█          ▄████████
	  ███    ███ ███  ███    ███   ███ ▄███▀ ███         ███    ███
	  ███    ███ ███▌ ███    █▀    ███▐██▀   ███         ███    █▀
	  ███    ███ ███▌ ███         ▄█████▀    ███        ▄███▄▄▄
	▀█████████▀  ███▌ ███        ▀▀█████▄    ███       ▀▀███▀▀▀
	  ███        ███  ███    █▄    ███▐██▄   ███         ███    █▄
	  ███        ███  ███    ███   ███ ▀███▄ ███▌    ▄   ███    ███
	 ▄████▀      █▀   ████████▀    ███   ▀█▀ █████▄▄██   ██████████
				       ▀         ▀

This is a copy, and modification, of a small interpreter written by Antirez in
about 500 lines of C, this interpreter is for a small TCL like language. The
blog post describing this interpreter can be found at
<http://oldblog.antirez.com/post/picol.html>, along with the code itself at
<http://antirez.com/picol/picol.c.txt>. It does a surprising amount for such a
small amount of code.

## License

The files [pickle.c][] and [pickle.h][] are licensed under the [BSD License][],
to keep things consistent [block.c][] and [block.h][] (which contains a memory
pool allocator) is also licensed under the 2 clause [BSD License][].

## Building

To build you will need a [C][] compiler and [Make][].

Type 'make' to build the executable 'pickle' (or 'pickle.exe') on Windows. To
run type 'make run', which will drop you into a pickle shell. 'make test' will
run the built in unit tests and the unit tests in [test.tcl][].

## The Picol Language

The internals of the interpreter do not deviate from the original interpreter,
so the document on the [picol][] language still applies. The language is like a
simplified version of [TCL][], where everything is a command and the primary
data structure is the string.

Some programmers seem to an obsessive interest in their language of choice,
do not become one of those programmers. This language, like any other
language, will not solve all of your problems and may be entirely unsuitable
for the task you want to achieve. It is up to you to evaluate whether this
language, and implementation of it, is suitable.

Language and implementation advantages:

* Small and compact, easy to integrate into a wide variety of platforms and
programs
* Fairly good at string handling
* Can be ported to a variety of platforms
* Customizable
* Suitable as a command language and shell

Disadvantages:

* Everything is a string (math operations and data structure manipulation
will be a pain).
* This is a Do-It-Yourself solution, it may require that you modify the library
itself and will almost certainly require that you define your own new commands
in [C][].
* The language interpreter is not well tested and is likely to be insecure. If
you find a bug, please report it.
* Despite being a language designed to manipulate strings it lacks many
expected string operations, they have to be added by the user. This includes
facilities for trimming strings, replacing subsections of strings,
concatenating strings, modifying bits of string, searching, and more.
* Lacks Unicode support.

Potential Improvements:

* Many, many more commands could be written in order to make this interpreter
more usable.
* A hash library could be integrated. It would not have to that big or complex
to greatly speed up the interpreter.
* The following small library can be used to either extend or modify the
interpreter to suite your purposes:
  - UTF-8: <https://www.cprogramming.com/tutorial/unicode.html>
  - Data Packing/Unpacking: <https://beej.us/guide/bgnet/html/multi/advanced.html#serialization>
  - Base-64: <https://stackoverflow.com/questions/342409/>
  - Line editing: <https://github.com/antirez/linenoise>

### The language itself

Picol, and [TCL][], are dynamic languages with only one real data type, the
string. This might seem inefficient but it is fine for a glue language whose
main purpose is to bind lots of things written in C together. It is similar to
[lisp][] in ways, it is [homoiconic][], and is simple with very little in the
way of syntax.

The following table sums up the different language constructs:

	string  called if first argument
	{ }     quote, used to prevent evaluation
	[ ]     command substitution
	" "     string
	$var    variable lookup
	\c      escape a character
	#       comment
	;       terminates a command

A Picol program consists of a series of commands and arguments to those
commands. Before a command is evaluated, variables are looked up and strings
substituted.

You may have noticed that things such as 'if' or 'while', and even procedure
definition, are not part of the languages syntax. Instead, they are built in
commands and are called like any other command.

Examples of commands:

	puts "Hello, World"
	"puts" "Hello, World"

	# prints "Hello, World"
	set cmd puts
	$cmd "Hello, World"

	# prints "Hello, World"
	set a pu
	set b ts
	$a$b "Hello, World"

	+ 2 2
	- 4 5
	if {bool 4} { puts "TRUE"}

	proc x {a b} { + $a $b }
	puts "x(3, 9) == [x 3 9]"

	# prints 4 to 10 inclusive
	set z 3
	while {< $z 10} { set z [+ $z 1]; puts $z }

To best understand the language, play around with it, and look at the source,
there really is not that much there.

### Internally Defined Commands

Picol defines the following commands internally, that is, they will always be
available.

The options passed to the command and type are indicated after the command, a
question mark suffix on an argument indicates an optional command.

All strings can be coerced into numbers, whether they are willing or not, for
example the strings 'iamnotanumber' becomes 0 (not 6), '3d20' to 3, and '-50'
to -50.

Truth is represented as a string that evaluates to a non zero number, false
as a string that evaluates to zero.

* set variable value?

Create a variable, or overwrite an existing variable, with a value. If only one
argument is given, it returns the value of that variable if it exists or an error
if it does not.

* if {condition} {true clause}  *OR*  if {condition} {true clause} else {false clause}

*if* is the command used to implement conditional execution of either one
clause, or one clause or (exclusive or) another clause. Like in every other
programming language ever (or more accurately the languages with more than one
user, the implementer).

* while {condition} {clause}

Keep executing the while clause whilst the condition is true (ie. is non-zero).

* break

Break out of a while loop. This will continue to break out of a things until
the return code is caught by a loop, or 'catch'.

* continue

Desist from executing the rest of the clause in a while loop, and go back to
testing the condition.

* proc identifier {argument list} {function body}

Create a new command with the name 'identifier', or function if you prefer,
with the arguments in 'argument list', and code to be executed in the 'function
body'. If the final command is not a 'return' then the result of the last
command is used.

* return string? number?

Optionally return a string, optionally with an internal number that can affect
control flow.

* uplevel number strings...

Evaluate the 'strings...' in the scope indicated by 'number'. A special case
is '#0', which is the global context. The strings are concatenated together as
with if they have been run through the 'concat' command. A scope of 0 is the
current scope, of 1, the caller, of 2, the caller's caller, and so on. A '#'
prefix is meant to reverse the search and start from the global scope and work
down through the call stack, however only '#0' is supported.

* upvar number otherVar myVar

Form a link from myVar to otherVar in the scope specified by number. A
special case is '#0', which is the global context, see 'uplevel' for a
description of the scoping traversal rules implied by the number argument.

You may have noticed that 'upvar' and 'uplevel', which come from [TCL][], are
strange, very strange. No arguments from me.

* unset string

Unset a variable, removing it from the current scope.

* concat strings...

Concatenate a list of strings with a space in-between them.

* eval strings...

Concatenate a list of strings with a space in-between them, as with 'concat',
then evaluate the string, returning the result of the evaluation.

* mathematical operations

The following mathematical operations are defined:

'+', '-', '\*', '/', '&lt;', '&lt;=', '&gt;', '&gt;=', '==', '!='. It should be
obvious what each one does. It should be noted that because all variables are
stored internally as strings, mathematical operations are egregiously slow.
Numbers are first converted to strings, the operation performed, then converted
back to strings.

There are also the following unary mathematical operators defined: '!', '~',
'abs', 'bool'.

* catch expr varname

This allows arbitrary codes to be caught, 'catch' evaluates an expression and
puts the return code into 'varname', the string returned is the result of the
evaluation of 'expr'.

* command item number *OR* command

This function is used to inspect the currently defined commands in the system.

If no arguments are given then the number of commands defined is returned. If
an item is given a number indicates which command that it applies to. Commands
are indexed by numbers. Defining new command may change the index of other
commands. Commands are either user defined or built in commands.

 - args: get a functions arguments (returns 'built-in' for built in commands)
 - body: get a functions body (returns 'built-in' for built in commands)
 - name: get a functions name

* join {list} String

Given a [TCL][] list, 'join' will flatten that list and return a string by
inserting a String in-between its elements. For example "join {a b c} ," yields
"a,b,c".

#### String Operator

* string option arg *OR* string option arg arg

The 'string' command in [TCL][] implements nearly every string command you
could possibly want, however this version of 'string' is more limited and
behaves differently in many circumstances. 'string' also pulls in more standard
C library functions from 'ctype.h' and 'string.h'.

Some of the commands that are implemented:

  - string match pattern String

This command is a primitive regular expression matcher, as available from
<http://c-faq.com/lib/regex.html>. What it lacks in functionality, safety and
usability, it makes up for by being only ten lines long (in the original). It
is meant more for wildcard expansion of file names (so '?' replaces the meaning
of '.' is most regular expression languages). '%' is used as an escape
character, which escapes the next character.

The following operations are supported: '\*' (match any string) and '?' (match
any character). By default all patterns are anchored to match the entire
string, but the usual behavior can be emulated by prefixing the suffixing the
pattern with '\*'.

  - string trimleft  String Class?

If 'class' is empty, a white-space class is used. 'trimleft' removes leading
characters in a Class from the given String.

  - string trimright String Class?

If 'class' is empty, a white-space class is used. 'trimleft' removes trailing
characters in a class from the given String.

  - string trim      String Class?

If 'class' is empty, a white-space class is used. 'trimleft' removes both
leading and trailing characters in a class from the given String.

  - string length  String

Get the length of String. This is a simple byte length excluding an ASCII NUL
terminator.

  - string tolower String

Convert an ASCII String to lower case.

  - string toupper String

Convert an ASCII String to upper case.

  - string reverse String

Reverse a string.

  - string equal   String1 String2

Compare two strings for equality. Returns '1' if equal, '0' if not equal. This
comparison is case sensitive.

  - string compare String1 String2

Compare two strings.

  - string index   String Index

Retrieve a character from a String at the specified Index. The index starts at
zero for the first character up to the last character. Indices past the last
character return the last character. Negative indexes starting counting from
the last character (the last character being -1) and count downward, negative
indexes that go before the first character return the first character.

  - string is Class String

'is' determines whether a given String belongs to a Class. Most class tests
accept a zero length string as matching that class with a few exceptions. Most
class tests test that a class contains only certain characters (such as 'alpha'
which checks that a string only contains the characters 'a-z' and 'A-Z', or
'digit', which checks that a string only contains the characters '0-9'. Other
class tests test that a string matches a specific format, such as 'integer'
(which does not accept a zero length string), it excepts the string to contain
a decimal number with an optional '+' or '-' prefix.

Class can be:

    - alnum
    - alpha
    - digit
    - graph
    - lower
    - print
    - punct
    - space
    - upper
    - xdigit
    - ascii
    - control 
    - integer

Any other Class is invalid.

  - string repeat String Count

Repeat a String 'Count' many times. 'Count' must be positive, inclusive of
zero.

  - string first Needle Haystack StartIndex?

Find a Needle in a Haystack, optionally starting from 'StartIndex'. The index
into the string where the first character of found of Needle in Haystack is
returned if the string has been found, negative one if it has not been found.

  - string ordinal String

Convert the first character in a string to a number that represents that
character.

  - string char Number 

Convert a number to its character representation.

  - string hex2dec HexString

Convert a lower or uppercase hexadecimal number to its decimal representation. 

  - string dec2hex Number

Convert a decimal number to its lowercase hexadecimal representation.

  - string hash String

Hash a string returning the hash of that string as a number.

  - string range String Index1 Index2

Create a sub-string from Index1 to Index2 from a String. If Index1 is greater
than Index2 an empty string is returned. If Index1 is less than zero, it is set
to zero, if Index2 is greater than the index of the last character, it is set
to the index of the last character. Indexing starts at zero and goes up to one
less than the strings length (or zero of empty string), which is the index of 
the last character. The characters from Index1 to Index2 inclusive form the
sub-string.

### Extension Commands

[main.c][] extends the interpreter with some commands that make the language
usable for demonstration purposes by including routines for Input and Output,
Time, Retrieving Environment Variables, and executing commands with the Systems
Shell.

The following commands are defined:

* puts -nonewline? string

Write a string, followed by a newline, to the standard output stream,
[stdout][]. If an EOF is encountered, a return code of '1' occurs and the
string 'EOF' is returned. '-nonewline' can be given as an optional argument, to
suppress the newline.

* error string

Write an error message to the standard error stream, followed by a newline and
return '1' for the return code.

* gets

Get a string from the standard input stream, [stdin][]. This returns a string
including a newline.

* system string?

Execute a command with the systems command interpreter, whatever that is. An
any decent Unixen this will be 'sh'. On any indecent Windows platform this will
be 'cmd.exe'. On MS-DOS this will be 'COMMAND.COM' (lol).

* exit number?

Exit from the interpreter, the number argument is optional and is coerced into
a number to be used as the exit code. This calls the C function 'exit', so
commands registered with 'atexit' will be called.

* quit number?

*quit* is a synonym for *exit*. It performs exactly the same function and is
only there because it is common to type either *quit* or *exit* in order to
leave an interpreter (I find it absurd that if you type 'exit' or 'quit'
into [python][] it knows exactly what you intended to do then does not do it.
Instead it taunts you with the correct answer).

* getenv string

Retrieve the contents of the environment variable named in string. This will
return the empty string on failure to locate the variable.

* random number?

Use whatever random number generator is available on your system to return a
random number. It should not be relied upon to produce good quality random
numbers. An optional number argument can be used to seed the pseudo random number
generator.

* clock format?

This command calls the [C][] function [strftime][] on GMT to create a new
string containing time information, or not, depending on what you have put in
the format string. The full details of the format string will not be specified
here, but here are a few examples:

	clock "Date/Time: %c"

Returns the String:

	"Date/Time: Sat Oct  6 11:32:59 2018"

When no argument is given the time since start of program execution is given.
On some systems this is the CPU time and not the total time that the program
has been executed.

* eq string string

Returns '0' is two strings are not equal and '1' if they are. Unlike '==' this
acts on the entire string.

* ne string string

Returns '1' is two strings are not equal and '0' if they are. Unlike '!=' this
acts on the entire string.

* raise number

Raise a signal, what this will do depends on your system, but it will most
likely kill the process. If it does not, it returns an integer indicating the
result of calling "raise()".

* signal number action *OR* signal

This is used to both set a signal handler and to query whether a signal has
been raised. If no arguments are given then the signal fired, or zero if
no signals have been detected, is returned. A subsequent call with zero
arguments will return zero (if a signal has not be fired since). Signal events
are not queued and can be lost.

If two arguments are given, the first is treated as a signal number and
the second is the action to perform.  There are three possible actions;
"ignore", "default" and "catch". A caught signal will set a variable
which can be queried by calling signal with no arguments, as already described.
An "ignored" signal will be ignored and a signal with "default" behavior will
use the systems defaults. On successful installation of the handler, a "1" is
returned, and on failure "0".

* argv number?

'argv' is a function at the moment, it could be made into a variable. If given
no arguments it will return the number of arguments passed to 'main()'. If a
number is given it will retrieved that argument from the argument list passed
to 'main()'.

* source file.tcl

Execute a file off disk, 'file.tcl' is the file to execute. This executes the
file in the current interpreter context and is *not* a safe operation.

* info item

Retrieve information about the system. Information items include:

 - level, call stack level
 - line, current line number
 - heap, information about the heap, if available, see 'heap' command.

But may include other information.

* heap item number *OR* heap item *OR* heap

The heap command is used to enquire about the status of the heap. Using the
command does change the thing it is measuring, however physics has the same
problem and physicists are doing pretty well. The command only works when the
custom allocator is used, as it interrogates it for the statistics it has
captured.

 - "freed": Number of calls to 'free'
 - "allocs": Number of calls to 'allocate'
 - "reallocs": Number of calls to 'realloc'
 - "active": Current active byte count
 - "max": Maximum number of bytes in use
 - "total": Total bytes request
 - "blocks": Total bytes given
 - "arenas": Number of arenas

These options require an argument; a number which species which allocation
arena to query for information.

 - "arena-size": Number of blocks
 - "arena-block": Size of a block
 - "arena-used": Number of blocks currently in use

* getch

Read a single character from standard in, returning the result as a number (you
can use "string char" to convert this to a character). -1 is returned on EOF.

* putch number

Write a character, represented numerically, to standard output. The original
character is returned if there is no error in doing this.

## Compile Time Options

I am not a big fan of using the [C Preprocessor][] to define a myriad of
compile time options. It leads to messy and unreadable code.

That said the following compile time options are available:

* NDEBUG

If defined this will disable assertions. It will also disable unit tests
functions from being compiled. Assertions are used heavily to check that the
library is being used correctly and to check the libraries internals, this
applies both to the block allocation routines and pickle itself.

This is all. Inevitably when an interpreter is made for a new language,
[readline][] (or [linenoise][]) integration is a build option, usually because
the author is tired of pressing the up arrow key and seeing '^\[\[A'. Naturally
this increases the complexity of the build system, adds more options, and adds
more code. Instead you can use [rlwrap][], or an alternative, as a wrapper
around your program.

## Custom Allocator / C Library usage

To aid in porting the system to embedded platforms, [pickle.c][] contains no
Input and Output functions (they are added in by registering commands in
[main.c][]). [pickle.c][] does include [stdio.h][], but only to access
[snprintf][]. The big problem with porting a string heavy language to an
embedded platform, unlike a language like [FORTH][], is memory allocation. It
is unavoidable that some kind of dynamic memory allocation is required. For
this purpose it is possible to provide your own allocator to the pickle
library. If an allocator is not provided, malloc will be used, you can remove
this from the initialization function in to stop your build system pulling in
your platforms allocator.

The block allocation library provided in [block.c][] can be optionally
used, but unlike [malloc][] will require tweaking to suite your purposes. The
maximum block size available to the allocator will also determine the maximum
string size that can be used by pickle.

Apart from snprintf, and sscanf, the other functions pulled in from the C
library are quite easy to implement. They include (but are not necessarily
limited to); strlen, memcpy, memchr, memset and abort.

## Interacting with the library and extension

The language can be extended by defining new commands in [C][] and registering
those commands with the *pickle\_register\_command* function. The internal
structures used are mostly opaque and can be interacted with from within the
language. As stated a custom allocator can be used and a block allocator is
provided, it is possible to do quite a bit with this scripting language whilst
only allocating about 32KiB of memory total on a 64-bit machine (for example
all of the unit tests and example programs run within that amount).

## To Do

* Add more internal unit tests, that test against the internals and against the
public facing API.
* Add documentation here; building, command line use, command list, syntax
and semantics. Make a nice manual page.
* Reorganize hashing code so it can be reused (and potentially reused as a
data structure within the interpreter).
* Add more commands in 'pickle.c' for command introspection, specifically so
that variables can be directly inspected and manipulated. Also allow
procedures to be renamed and deleted.
* C++ Example, integrating the interpreter in ways which make sense with the
language.
* Replace unsafe string copying routines with safe version; including 'strncpy'
(what's the point of this function!?).
* Currently there is no way to serialize the interpreter state, this could
actually be done from within the interpreter if methods for analyzing variables
were available.
* The get/set variable functions also need to be able to set a scope.
* Add a function to remove commands. This could be used to implement adding
resources such as file-handles, a file handle would consist of a function and
some private data. It would act something like this:

	set file [open "file.txt"]
	puts [$file -get]
	$file -close

That interface is not too cumbersome.


[pickle.c]: pickle.c
[pickle.h]: pickle.h
[block.c]: block.c
[block.h]: block.h
[main.c]: main.c
[picol]: http://oldblog.antirez.com/post/picol.html
[TCL]: https://en.wikipedia.org/wiki/Tcl
[stdio.h]: http://www.cplusplus.com/reference/cstdio/
[snprintf]: http://www.cplusplus.com/reference/cstdio/snprintf/
[FORTH]: https://en.wikipedia.org/wiki/Forth_(programming_language)
[C]: https://en.wikipedia.org/wiki/C_%28programming_language%29
[Make]: https://en.wikipedia.org/wiki/Make_(software)
[MIT License]: https://en.wikipedia.org/wiki/MIT_License
[BSD License]: https://en.wikipedia.org/wiki/BSD_licenses
[malloc]: https://en.wikipedia.org/wiki/C_dynamic_memory_allocation
[C Preprocessor]: https://en.wikipedia.org/wiki/C_preprocessor
[python]: https://www.python.org/
[stdout]: http://www.cplusplus.com/reference/cstdio/stdout/
[stdin]: http://www.cplusplus.com/reference/cstdio/stdin/
[strftime]: http://www.cplusplus.com/reference/ctime/strftime/
[readline]: https://tiswww.case.edu/php/chet/readline/rltop.html
[linenoise]: https://github.com/antirez/linenoise
[rlwrap]: https://linux.die.net/man/1/rlwrap
[lisp]: https://en.wikipedia.org/wiki/Lisp_(programming_language)
[homoiconic]: https://en.wikipedia.org/wiki/Homoiconicity
