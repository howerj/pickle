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
simplified version of [tcl][], where everything is a command and the primary
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
* A hash library could be integrate. It would not have to that big or complex 
to greatly speed up moderately complex programs.
* The following small library can be used to either extend or modify the
interpreter to suite your purposes:
  - UTF-8: <https://www.cprogramming.com/tutorial/unicode.html>
  - Data Packing/Unpacking: <https://beej.us/guide/bgnet/html/multi/advanced.html#serialization>
  - Base-64: <https://stackoverflow.com/questions/342409/>


### Internally Defined Commands

Picol defines the following commands internally, that is, they will always be
available.

The options passed to the command and type are indicated after the command, a
question mark suffix on an argument indicates an optional command.

All strings can be coerced into numbers, whether they are willing or not, for
example the strings 'iamnotanumber' becomes 0 (not 6), '3d20' to 3, and '-50'
to -50.

Truth is represented as a non zero string, false as a string that evaluates to
zero.

* set variable value

Create a variable, or overwrite an existing variable, with a value.

* if {condition} {true clause}  *OR*  if {condition} {true clause} else {false clause}

*if* has two forms.

* while {condition} {clause}

Keep executing the while clause whilst the condition is true (ie. is non-zero).

* break

Break out of a while loop.

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

* upvar number otherVar myVar

Form a link from myVar to otherVar in the context specified by number. A
special case is '#0', which is the global context.

* uplevel number strings...

Evaluate the 'strings...' in the context indicated by 'number'. A special case
is '#0', which is the global context.

* unset string

Unset a variable, removing it.

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

### Extension Commands

[main.c][] extends the interpreter with some commands that make the language
usable for demonstration purposes by including routines for Input and Output,
Time, Retrieving Environment Variables, and executing commands with the Systems
Shell.

The following commands are defined:

* puts string

Write a string, followed by a newline, to the standard output stream,
[stdout][].

* gets

Get a string from the standard input stream, [stdin][]. This returns a string
including a newline.

* system string?

Execute a command with the systems command interpreter, whatever that is. An
any decent Unixen this will be 'sh'. On any indecent Windows platform this will
be 'cmd.exe'. On MS-DOS this will be 'COMMAND.COM' (lol).

* exit number?

Exit from the interpreter, the number argument is optional and is coerced into
a number to be used as the exit code.

* quit number?

*quit* is a synonym for *exit*. It performs exactly the same function and is
only there because it is common to type either *quit* or *exit* in order to
leave an interpreter (I find it absurd that if you type 'exit' or 'quit' 
into [python][] it knows exactly what you intended to do then does not do it.
Instead it taunts you with the correct answer).

* getenv string

Retrieve the contents of the environment variable named in string. This will
return the empty string on failure to locate the variable.

* random

Use whatever random number generator is available on your system to return a
random number.

* strftime format

This command calls the [C][] function [strftime][] on GMT to create a new
string containing time information, or not, depending on what you have put in
the format string. The full details of the format string will not be specified
here, but here are a few examples:

	strftime "Date/Time: %c"

Returns the String:

	"Date/Time: Sat Oct  6 11:32:59 2018"

* match pattern string

This command is a primitive regular expression matcher, as available from
<http://c-faq.com/lib/regex.html>. What it lacks in functionality, safety and
usability, it makes up for by being only ten lines long (in the original). It
is meant more for wildcard expansion of file names (so '?' replaces the meaning
of '.' is most regular expression languages). Escape characters are not
supported.

The following operations are supported: '\*' (match any string) and '?' (match
any character). By default all patterns are anchored to match the entire
string, but the usual behavior can be emulated by prefixing the suffixing the
pattern with '\*'.

* eq string string

Returns '0' is two strings are not equal and '1' if they are. Unlike '==' this
acts on the entire string.

* length string

Returns the length of a string as a number.

* raise number

Raise a signal, what this will do depends on your system, but it will most
likely kill the process. If it does not, it returns an integer indicating the
result of calling "raise()".

## Compile Time Options

I am not a big fan of using the [C Preprocessor][] to define a myriad of
compile time options. It leads to messy and unreadable code.

That said the following compile time options are available:

* NDEBUG

If defined this will disable assertions. It will also disable unit tests
functions from being compiled. Assertions are used heavily to check that the
library is being used correctly and to check the libraries internals, this
applies both to the block allocation routines and pickle itself.

This is all.

## Custom Allocator

To aid in porting the system to embedded platforms, [pickle.c][] contains no
Input and Output functions (they are added in by registering commands in
[main.c][]). [pickle.c][] does include [stdio.h][], but only to access
[snprintf][]. The big problem with porting a string heavy language to an
embedded platform, unlike a language like [FORTH][], is memory allocation. It
is unavoidable that some kind of dynamic memory allocation is required. For
this purpose it is possible to provide your own allocator to the pickle
library. 

The block allocation library provided in [block.c][] can be optionally
used, but unlike [malloc][] will require tweaking to suite your purposes. The
maximum block size available to the allocator will also determine the maximum
string size that can be used by pickle.

## Interacting with the library and extension

The language can be extended by defining new commands in [C][] and registering
those commands with the *pickle\_register\_command* function.

## To Do

* Make unit tests; both in Pickle source and in C
* Make the library suitable for embedded use (by reducing memory usage and
allowing the use of a custom allocator).
* Add documentation here; building, command line use, command list, syntax
and semantics.
* Reduce allocations by interning common strings like "", "1", "0", "if",
"return", and "Out-Of-Memory".
* Profile, profile, profile!
* Remove errors occurring from out of memory situations. These can be tested
by modifying the custom allocator.

[pickle.c]: pickle.c
[pickle.h]: pickle.h
[block.c]: block.c
[block.h]: block.h
[main.c]: main.c
[picol]: http://oldblog.antirez.com/post/picol.html
[tcl]: https://en.wikipedia.org/wiki/Tcl
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
