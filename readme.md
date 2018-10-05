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

## To Do

* Add more useful commands
* Make unit tests; both in Pickle source and in C
* Make the library suitable for embedded use (by reducing memory usage and
allowing the use of a custom allocator).
* Add documentation here; building, command line use, command list, syntax
and semantics.

