% pickle(1) | A Small TCL like interpreter

# NAME

PICKLE - A Small and Embeddable TCL like interpreter and library

# SYNOPSES

pickle files...

pickle

# DESCRIPTION

	Author:     Richard James Howe / Salvatore Sanfilippo
	License:    BSD
	Repository: <https://github.com/howerj/pickle>
	Email:      howe.r.j.89@gmail.com
	Copyright:  2007-2016 Salvatore Sanfilippo
	Copyright:  2018-2020 Richard James Howe

This is a copy, and modification, of a small interpreter written by Antirez in
about 500 lines of C, this interpreter is for a small TCL like language. The
blog post describing this interpreter can be found at
<http://oldblog.antirez.com/post/picol.html>, along with the code itself at
<http://antirez.com/picol/picol.c.txt>. It does a surprising amount for such a
small amount of code. This project is a little bit bigger than the original at
around ~6000 lines.

## LICENSE

The files [pickle.c][] and [pickle.h][] are licensed under the 2 clause
[BSD License][], as are all the other files in this project.

## BUILDING

To build you will need a [C][] compiler and [Make][].

Type 'make' to build the executable 'pickle' (or 'pickle.exe') on Windows. To
run type 'make run', which will drop you into a pickle shell. 'make test' will
run the built in unit tests and the unit tests in [shell][].

## RUNNING

To run the project you will need to build it, the default makefile target will
do this, type:

	make

Or:

	make pickle

This will build the pickle library and then link this library with an example
program contained in [main.c][]. This example program is very simple and adds a
few commands to the interpreter that do not exist in the library ("gets", "puts",
"clock", "getenv", "exit", "source", "clock" and "heap"), this is the minimal
set of commands that are needed to get a usable shell up and running and do
performance optimization.

The executable 'pickle' that is built is quite simple, it executes all arguments
given to it on given to it on the command line as scripts. There are no
options, flags or detection of an interactive session with [isatty][]. This
makes usage of the interpreter in interactive sessions challenging, instead,
the language itself can be used to define a shell and process command line
arguments. This is done in a program called '[shell][]'. As mentioned it
contains the unit tests for the project, as well as other subprograms, and most
importantly it contains the interactive shell that reads a line at a time and
prints the result.

The code in [shell][] is quite large, if you do not want to use it an
incredibly minimal shell can be defined with the code:

	#!./pickle

	set r 0
	while { } {
		puts -nonewline "pickle> "
		set r [catch [list eval [gets]] v]
		puts "\[$r\] $v"
	}

For those experienced with [TCL][] some differences in the 'while' and 'gets'
command should be apparent.

## The Picol Language

The internals of the interpreter do not deviate much from the original
interpreter, so the document on the [picol][] language still applies. The
language is like a simplified version of [TCL][], where everything is
a command and the primary data structure is the string.

Some programmers seem to an obsessive interest in their language of choice,
do not become one of those programmers. This language, like any other
language, will not solve all of your problems and may be entirely unsuitable
for the task you want to achieve. It is up to you to evaluate whether this
language, and implementation of it, is suitable.

Language and implementation advantages:

* Small and compact, easy to integrate into a wide variety of platforms and
programs. (4000 [LoC][] is the limit for the core library in [pickle.c][]).
* Fairly good at string handling.
* Can be ported to a variety of platforms. There are few system dependencies
and the design of the program allows it to be ported to an embedded platform.
* Customizable.
* Suitable as a command language and shell.

Disadvantages:

* Everything is a string (math operations and data structure manipulation
will be **slow**).
* This is a Do-It-Yourself solution, it may require that you modify the library
itself and will almost certainly require that you define your own new commands
in [C][].
* Lacks Unicode/UTF-8 support.
* The language interpreter is not well tested and is likely to be insecure. If
you find a bug, please report it. It is however better tested that most
(re)implementations or extensions to the [picol][] interpreter and far less
likely to segfault or crash if you misuse the interpreter, aggressive fuzzing
using American Fuzzy Lop helped iron out most of the egregious bugs.

Potential Improvements:

* Many, many more commands could be written in order to make this interpreter
more usable.
* The following small libraries can be used to either extend or modify the
interpreter to suite your purposes:
  - UTF-8: <https://www.cprogramming.com/tutorial/unicode.html>
  - Data Packing/Unpacking: <https://beej.us/guide/bgnet/html/multi/advanced.html#serialization>
  - Base-64: <https://stackoverflow.com/questions/342409/>
  - Line editing: <https://github.com/antirez/linenoise>
  - Fixed point (Q16.16, signed) library: <https://github.com/howerj/q>

The interpreter is fairly small, on my 64-bit x86 machine the interpreter
weighs in at 100KiB (stripped, dynamically linked to C library, Linux ELF). The
interpreter can be configured so that it is even smaller, for example:

	On Debian Linux, x86_64, dyanmically linking against glibc, using
	gcc version 8.3.0, with version 4.1.4 of the interpreter:
	Size    Options/Notes
	100KiB  Normal target, optimized for speed (-O2).
	84KiB   No debugging (-DNDEBUG), optimized for speed (-O2).
	54KiB   No debugging (-DNDEBUG), 32-bit target (-m32), optimized
	        for size (-Os), stripped, No features disabled.
	34KiB   No debugging (-DNDEBUG), 32-bit target, optimized for size,
	        stripped, with as many features disabled as possible.

	On Debian Linux, x86_64, statically linked against musl C library:
	147KiB  Normal target, optimized for speed, statically linked.

This is still larger than I would like it to be, the original picol interpreter
in the smallest configuration (32 bit target, optimized for size), comes in at
18KiB.

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

Picol defines the commands in this section internally, in a default build all
of the commands in this section will be available. There are some build options
to remove some commands (such as the string function, the math functions, and
the list functions).

The options passed to the command and type are indicated after the command, a
question mark suffix on an argument indicates an optional command, an ellipsis
indicates an optional series of arguments.

For some concrete examples of commands being run, see [unit.tcl][], which
contains [unit tests][] for the project.

The list of internally defined commands is as follows;

	apply, break, catch, concat, conjoin, continue, eq,
	eval, for, if, incr, info, join, list, ne, proc,
	rename, return, set, subst, trace, unset, uplevel,
	upvar, while.

These are optionally defined depending on the build configuration:

	lappend, lindex, linsert, llength, lrange, lrepeat,
	lreplace, lreverse, lsearch, lset, lsort, split, reg,
	string, !=, &, &&, *, +, -, /, <, <=, ==,
	>, >=, ^, and, log, lshift, max, min, mod, or,
	pow, rshift, xor, |, ||, !, abs, bool, invert,
	negate, not, ~.

They, along with the extension commands, used to be described within this
document, however they have since been incorporated into an on-line (in the
sense that the documentation is available when the system is on-line and
running, and not in the interactive-super-highway sense) help system.

The arithmetic operators are commands and not arguments to the "expr"
command, but most other functions try to behave as similar, if not
cut-down versions, of their TCL equivalents.

You can access the help system by using the "help" in the shell,
for example "help apply".

These commands are present in the [main.c][] file and have been added to the
interpreter by extending it. They deal with I/O, the list of these commands
is:

	gets, puts, getenv, exit, source, clock, heap.

The list of commands is short (there is *so* much more that could be
implemented) as the commands that are needed are the minimal set
required to create the unit test framework and shell application
present in the file [shell][].

## Compile Time Options

I am not a big fan of using the [C Preprocessor][] to define a myriad of
compile time options. It leads to messy and unreadable code.

That said the following compile time options are available:

* NDEBUG

If defined this will disable assertions. It will also disable unit tests
functions from being compiled. Assertions are used heavily to check that the
library is being used correctly and to check the libraries internals, this
applies both to the block allocation routines and pickle itself.

There are other compile time options within [pickle.c][] that control;
maximum string length and whether to use one, whether to provide the default
allocator or not, whether certain functions are to be made available to the
interpreter or not (such as the command 'string', the mathematical operators
and the list functions), and whether strict numeric conversion is used.
These options are semi-internal, they are subject to change and removal, you
should use the source to determine what they are and be aware that they may
change across releases.

Inevitably when an interpreter is made for a new language,
[readline][] (or [linenoise][]) integration is a build option, usually because
the author is tired of pressing the up arrow key and seeing '^\[\[A'. Naturally
this increases the complexity of the build system, adds more options, and adds
more code. Instead you can use [rlwrap][], or an alternative, as a wrapper
around your program.

## Custom Allocator / C Library usage

To aid in porting the system to embedded platforms, [pickle.c][] contains no
Input and Output functions (they are added in by registering commands in
[main.c][]). [pickle.c][] does include [stdio.h][], but only to access
[vsnprintf][]. The big problem with porting a string heavy language to an
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

Apart from [vsnprintf][], the other functions pulled in from the C
library are quite easy to implement. They include (but are not necessarily
limited to); strlen, memcpy, memchr, memset and abort.

## C API

The language can be extended by defining new commands in [C][] and registering
those commands with the *pickle\_command\_register* function. The internal
structures used are mostly opaque and can be interacted with from within the
language. As stated a custom allocator can be used and a block allocator is
provided, it is possible to do quite a bit with this scripting language whilst
only allocating about 32KiB of memory total on a 64-bit machine (for example
all of the unit tests and example programs run within that amount).

The C API is small and regular. All of the functions exported return the same
error codes and implementing an interpreter loop is trivial.

The language can be extended with new functions written in C, each function
accepts an integer length, and an array of pointers to ASCIIZ strings - much
like the 'main' function in C.

User defined commands can be registered with the 'pickle\_command\_register'
function. With in the user defined callbacks the 'pickle\_result\_set' family of
functions can be used. The callbacks passed to 'pickle\_command\_set' look
like this:

	typedef int (*pickle_func_t)(pickle_t *i, int argc, char **argv, void *privdata);

The callbacks accept a pointer to an instance of the pickle interpreter, and
a list of strings (in 'argc' and 'argv'). Arbitrary data may be passed to the
custom callback when the command is registered.

The function returns one of the following status codes:

	PICKLE_ERROR    = -1 (Throw an error until caught)
	PICKLE_OK       =  0 (Signal success, continue execution)
	PICKLE_RETURN   =  1 (Return out of a function)
	PICKLE_BREAK    =  2 (Break out of a while loop)
	PICKLE_CONTINUE =  3 (Immediately proceed to next iteration of while loop)

These error codes can affect the flow control within the interpreter. The
actual return string of the callback is set with 'pickle\_result\_set' functions.

Variables can be set either within or outside of the user defined callbacks
with the 'pickle\_var\_set' family of functions.

The pickle library does not come with many built in functions, and comes with
no Input/Output functions (even those available in the C standard library) to
make porting to non-hosted environments easier. The example test driver program
does add functions available in the standard library.

The following is the source code for a simple interpreter loop that reads a
line and then evaluates it:

	#include "pickle.h"
	#include <stdio.h>
	#include <stdlib.h>

	static void *allocator(void *arena, void *ptr, size_t oldsz, size_t newsz) {
		if (newsz ==     0) { free(ptr); return NULL; }
		if (newsz  > oldsz) { return realloc(ptr, newsz); }
		return ptr;
	}

	static int prompt(FILE *f, int err, const char *value) {
		if (fprintf(f, "[%d]: %s\n> ", err, value) < 0)
			return -1;
		return fflush(f) < 0 ? -1 : 0;
	}

	int main(void) {
		pickle_t *p = NULL;
		if (pickle_new(&p, allocator, NULL) < 0)
			return 1;
		if (prompt(stdout, 0, "") < 0)
			return 1;
		for (char buf[512] = { 0 }; fgets(buf, sizeof buf, stdin);) {
			const char *r = NULL;
			const int er = pickle_eval(p, buf);
			if (pickle_result_get(p, &r) != PICKLE_OK)
				return 1;
			if (prompt(stdout, 0, r) < 0)
				return 1;
		}
		return pickle_delete(p);
	}

It should be obvious that the interface presented is not efficient for many
uses, treating everything as a string has a cost. It is however simple and
sufficient for many tasks.

While API presented in 'pickle.h' is small there are a few areas of
complication. They are: The memory allocation API, registering a command, the
getopt function and the unit tests. The most involved is the memory allocation
API and there is not too much to it, you do not even need to use it and can
pass a NULL to 'pickle\_new' if for the allocator argument if you want to use
the built in malloc/realloc/free based allocator (provided the library was
built with support for it).

It may not be obvious from the API how to go about designing functions to
integrate with the interpreter. The C API is deliberately kept as simple as
possible, more could be exported but there is a trade-off in doing so; it
places more of a burden on backwards compatibility, limits the development
of the library internals and makes the library more difficult to use. It is
always possible to hack your own personal copy of the library to suite your
purpose, the library is small enough that this should be possible.

The Pickle interpreter has no way of registering different types with it, the
string is king. As such, it is not immediately clear what the best way of
adding functionality that requires manipulating non-string data (such as
file handles or pointers to binary blobs) is. There are several ways of doing
this:

1. Convert the pointer to a string and add functions which deal with this string.
2. Put data into the private data field
3. Create a function which registers another function that contains private data.

Option '1' may seem natural, but it is much more error prone. It is possible
to pass the wrong string around and cause the program to crash. Option '2' is
limiting, the C portion of the program is entirely in control of what resources
get added, and only one handle to a resource can be controlled. Option '2' is
a good option for certain cases.

Option '3' is the most general and allows an arbitrary resource to be managed
by the interpreter. The idea is to create a function that acquires the resource
to be managed and registers a new function in the pickle global function
namespace with the resource in the private data field of the newly registered
function. The newly created function, a limited form of a closure, can then
perform operations on the handle. It can also cleanup the resource by release
the object in its private data field, and then deleting itself with the
 'pickle\_command\_rename' function. An example of this is the 'fopen' command,
it returns a closure which contains a file handle.

An example of using the 'fopen' command and the returned function from within
the pickle interpreter is:

	set fh [fopen file.txt rb]
	set line [$fh -gets]
	$fh -close

And an example of how this might be implemented in C is:

	int pickleCommandFile(pickle_t *i, int argc, char **argv, void *pd) {
		FILE *fh = (FILE*)pd;
		if (!strcmp(argv[1], "-close")) { /* delete self */
			fclose(fh);                                /* free handle */
			return pickle_command_rename(argv[0], ""); /* delete self */
		}
		if (!strcmp(argv[1], "-gets")) {
			char buf[512];
			fgets(buf, sizeof buf, fh);
			return pickle_result_set(i, "%s", buf);
		}
		return pickle_result_set(i, PICKLE_ERROR, "invalid option");
	}

	int pickleCommandFopen(pickle_t *i, int argc, char **argv, void *pd) {
		char name[64];
		FILE *fh = fopen(argv[1], argv[2]);
		sprintf(name, "%p", fh); /* unique name */
		pickle_command_register(i, name, pickleCommandFile, fh);
		return pickle_set_result(i, "%s", name);
	}

The code illustrates the point, but lacks the assertions, error checking,
and functionality of the real 'fopen' command. The 'pickleCommandFopen' should
be registered with 'pickle\_command\_register', the 'pickleCommandFile' should
not be registered as 'pickleCommandFopen' does the registering when needed.

It should be possible to implement the commands 'update', 'after' and 'vwait',
extending the interpreter with task management like behavior without any changes
to the API. It is possible to implement most commands, although it might
be awkward to do so. Cleanup is still a problem.

## Style Guide

Style/coding guide and notes, for the file [pickle.c][]:

- 'pickle\_' and snake\_case is used for exported functions/variables/types
- 'picol'  and camelCase  is used for internal functions/variables/types,
with a few exceptions, such as 'advance' and 'compare', which are internal
functions whose names are deliberately kept short.
- Use asserts wherever you can for as many preconditions, postconditions
and invariants that you can think of.
- Make sure you make your functions static unless they are meant to
be exported. You can use 'objdump -t | awk '$2 ~ /[Gg]/' to find
all global functions. 'objdump -t | grep '\\\*UND\\\*' can be used to
check that you are not pulling in functions from the C library you
do not intend as well.
- The core project is written strictly in C99 and uses only things
that can be found in the standard library, and only things that are
easy to implement on a microcontroller.
- Error messages should begin with the string 'Invalid', even it is
does not make the best grammatical sense, this is so error messages can
be grepped for easily.

The callbacks all have their 'argv' argument defined as 'char\*',
as they do not modify their arguments. However adding this in just adds
a lot of noise to the function definitions. Also see
<http://c-faq.com/ansi/constmismatch.html>.

## Notes

* Single File implementation

There is the concept of a "[Header-only Library][]", needed because C/C++
lacks a module system (The objects formats also elide type information which
makes interfacing with C a pain, so much heartache could have been avoided
if the compromise of the C Preprocessor was avoided!). This library is a
prime candidate for turning into a Header-Only library.

* Other implementations

There are other implementations of [TCL][] and other extensions of the original
[picol][] interpreter, here are a few:

- Picol Extension <https://wiki.tcl-lang.org/page/Picol>
- TCL reimplementation <http://jim.tcl.tk/index.html/doc/www/www/index.html>
- An entire list of implementations <https://blog.tcl.tk/17975>
- Another Picol Extension <https://chiselapp.com/user/dbohdan/repository/picol/index>
- Partcl <https://github.com/zserge/partcl>, a complete reimplementation, also
with a blog post <https://zserge.com/posts/tcl-interpreter/> describing it.

And I am sure if you were to search <https://github.com> you would find more.

* Internal memory usage

One of the goals of the interpreter is low(ish) memory usage, there are a few
design decisions that go against this, along with the language itself, however
an effort has been made to make sure memory usage is kept low.

Some of the (internal) decisions made:

- The use of 'compact\_string\_t' where possible. This can be used to store a
  string within a union of a small character array or a pointer, this requires
  a bit be available elsewhere to store which is used.
- Compact, small, structures for structures that are used a lot; call frames (2
  pointers), variables (3 pointers and a bit-field), and commands (4 pointers).
- Linked-lists are used, which increase overall memory usage but mean large
  chunks of memory do not have to be allocated and reallocate for things like
  tables of functions and variables.
- The parser operates on a full program string and tokens to the string a
  indices into the string, which means a large [AST][] does not
  have to be assembled.
- Memory is allocated on the stack where possible, with the function
  'picolStackOrHeapAlloc' helping with this, it moves allocations to the
  heap if they become too large for the stack. This could conceivably be used
  for more allocations we do, for example when creating argument lists, but is
  currently just used for some unbounded string operations. (Of note, it might
  be worth creating memory pools for small arguments lists as they are
  generated fairly often, or even a pool of medium size buffers we could lock).
  This could also speed things up as we spend a lot of time allocating objects,
  the working set may be small but the number of temporary objects created is
  not, 'picolArgsGrow' could be targeted for this.
- The empty string could be treated specially by the interpreter and all empty
  strings never freed nor allocated.
- Also of note is that the interpreter is designed to gracefully handle out of
  memory conditions, it may not live up to this fact, but it is possible to
  test this by returning NULL in the allocator provided randomly.
- There dictionary is currently initialized at startup from a list of functions
  and their names, this takes up RAM and not ROM, a two level hashing function
  could be used instead. Calling 'rename' on these built-in functions could
  be handled with a single bit per entry separate from the built-in function
  hash table, or by disallowing 'rename' on built in functions. A not trivial
  (for an embedded system) amount of memory is used by this table. The
  trade-off is more lines of code, more complexity, and a bigger executable.

Some of the design decisions made that prevent and hamper memory usage and
things that could be done:

- Defined procedures are strings and Lack of Byte Code Compilation

It would be possible to include some simple complication on the procedures that
are stored, turning certain keywords into bytes codes that fall outside of the
[UTF-8][] and [ASCII][] character ranges, as well as removing runs of white
space and comments entirely. This would be possible to implement without
changing the interface and would both speed things up and reduce memory usage,
however it would increase the complexity of the implementation (perhaps by
about 500 LoC if that can be thought of as a proxy for complexity).

- Within [pickle.c][] there is a table of functions that are register on
  startup, registering means taking each entry in this array and entering it
  into a hash-table that contains all of the other defined procedures and
  built-in functions, there is obviously some redundancy here. It might be
  worth treating the built-in core functions specially with a binary search
  tree just for them instead of adding them into the hash table (this would
  complicate the 'rename' command as well).

* [vsnprintf][]

If you need an implementation of [vsnprintf][] the [Musl C library][] has one.
This is the most complicate C function in use from the standard library and the
one most likely not to be available in an embedded platform (although the base
software packages are getting better nowadays). It is not difficult to make
your own version of [vsnprintf][] function usable by this library as you do not
need to support all of the functionality library function, for example,
floating point numbers are not used within this library.

* Too big

The list functions are also far to complex, big, and error prone, they should
be rewritten.

It might be nice to go back to the original source, with what I know now, and
create a very small version of this library with a goal of compiling to under
30KiB. The 'micro' makefile target does this somewhat, or just starting from
scratch and making my own version. A smaller API could be made as well, there
really only needs to be; pickle\_new, pickle\_delete, pickle\_eval,
pickle\_command\_register, and pickle\_result\_set.

* A module system and some modules

This interpreter lacks a module system, there are a few small and simple
modules that could be integrated with the library quite easily, see;
Constant Data Base Library <https://github.com/howerj/cdb>, A HTTP 1.1
client <https://github.com/howerj/httpc>, Tiny compression routines
<https://github.com/howerj/shrink>, and a fixed point arithmetic
library <https://github.com/howerj/q>, and UTF-8 string handling
<https://github.com/howerj/utf8> These would have to be external modules
that could be integrated with this library.

The current project that attempts to remedy this is available at:

<https://github.com/howerj/mod-pickle>

A proper module system would also allow Shared Objects / Dynamically Linked
Libraries to be loaded at run time into the interpreter. This complicates the
library, but a Lisp Interpreter where I have done this, see
<https://github.com/howerj/liblisp>.

* Things that are missing that will not be added.

 - The [coroutine][] words are missing, which will require interpreter
 support to implement efficiently. If you need coroutines (which are very
 useful) then access to the internals is needed.
 - Event handling in [vwait][] and [update][] and the like could be added
 later on.
 - The control structures 'foreach' and 'switch' - whilst very useful - will
 most likely not be added. There is no reason that they cannot be added as
 extensions however.
 - Lack of Floating point support, which should not really be expected either
 given the primary usage for this interpreter, as a command language in
 embedded devices.

## Interpreter Limitations

Known limitations of the interpreter include:

* Recursion Depth - 128, set via a compile time option.
* Maximum size of file - 2GiB.
* 'clock' command has a limited string available for formatting (512 bytes).
* Although not tested and there are no known issues there might be
[locale][] related issues lurking in the interpreter, it does not help
that the locale functions are fundamentally broken and should never have
been standardized as they are (or at all).

[ASCII]: https://en.wikipedia.org/wiki/ASCII
[AST]: https://en.wikipedia.org/wiki/Abstract_syntax_tree
[BSD License]: https://en.wikipedia.org/wiki/BSD_licenses
[C Preprocessor]: https://en.wikipedia.org/wiki/C_preprocessor
[C]: https://en.wikipedia.org/wiki/C_%28programming_language%29
[FORTH]: https://en.wikipedia.org/wiki/Forth_(programming_language)
[Lua]: https://www.lua.org/
[MIT License]: https://en.wikipedia.org/wiki/MIT_License
[Make]: https://en.wikipedia.org/wiki/Make_(software)
[Musl C library]: https://git.musl-libc.org/cgit/musl/tree/src/stdio
[TCL trace command]: https://www.tcl.tk/man/tcl8.5/TclCmd/trace.htm
[TCL]: https://en.wikipedia.org/wiki/Tcl
[UTF-8]: https://en.wikipedia.org/wiki/UTF-8
[alnum]: http://www.cplusplus.com/reference/cctype/isalnum/
[alpha]: http://www.cplusplus.com/reference/cctype/isalpha/
[cdb]: https://github.com/howerj/cdb
[control]: http://www.cplusplus.com/reference/cctype/iscntrl/
[coroutine]: <https://www.tcl.tk/man/tcl8.7/TclCmd/coroutine.htm>
[ctype.h]: http://www.cplusplus.com/reference/cctype/
[digit]: http://www.cplusplus.com/reference/cctype/isdigit/
[graph]: http://www.cplusplus.com/reference/cctype/isgraph/
[homoiconic]: https://en.wikipedia.org/wiki/Homoiconicity
[insertion sort]: https://en.wikipedia.org/wiki/Insertion_sort
[linenoise]: https://github.com/antirez/linenoise
[lisp]: https://en.wikipedia.org/wiki/Lisp_(programming_language)
[loc]: https://en.wikipedia.org/wiki/Source_lines_of_code
[lower]: http://www.cplusplus.com/reference/cctype/islower/
[main.c]: main.c
[malloc]: https://en.wikipedia.org/wiki/C_dynamic_memory_allocation
[pickle.c]: pickle.c
[pickle.h]: pickle.h
[picol]: http://oldblog.antirez.com/post/picol.html
[print]: http://www.cplusplus.com/reference/cctype/isprint/
[punct]: http://www.cplusplus.com/reference/cctype/ispunct/
[python]: https://www.python.org/
[readline]: https://tiswww.case.edu/php/chet/readline/rltop.html
[readme.md]: readme.md
[regex]: http://c-faq.com/lib/regex.html
[rlwrap]: https://linux.die.net/man/1/rlwrap
[shell]: shell
[snprintf]: http://www.cplusplus.com/reference/cstdio/snprintf/
[space]: http://www.cplusplus.com/reference/cctype/isspace/
[stdin]: http://www.cplusplus.com/reference/cstdio/stdin/
[stdio.h]: http://www.cplusplus.com/reference/cstdio/
[stdout]: http://www.cplusplus.com/reference/cstdio/stdout/
[strftime]: http://www.cplusplus.com/reference/ctime/strftime/
[string.h]: http://www.cplusplus.com/reference/cstring/
[unit tests]: https://en.wikipedia.org/wiki/Unit_testing
[unit.tcl]: unit.tcl
[update]: https://www.tcl.tk/man/tcl8.4/TclCmd/update.htm
[upper]: http://www.cplusplus.com/reference/cctype/isupper/
[vsnprintf]: http://www.cplusplus.com/reference/cstdio/vsnprintf/
[vwait]: https://www.tcl.tk/man/tcl8.4/TclCmd/vwait.htm
[xdigit]: http://www.cplusplus.com/reference/cctype/isxdigit/
[isatty]: http://man7.org/linux/man-pages/man3/isatty.3.html
[locale]: https://github.com/mpv-player/mpv/commit/1e70e82baa9193f6f027338b0fab0f5078971fbe
[Header-only Library]: https://en.wikipedia.org/wiki/Header-only
[American Fuzzy Lop]: https://lcamtuf.coredump.cx/afl/
