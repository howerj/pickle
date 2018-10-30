/**@file pickle.h
 * @brief pickle language header
 *
 * <https://github.com/howerj/pickle>
 *
 * A small TCL interpreter, called Pickle, that is basically just a copy
 * of the original written by Antirez, the original is available at
 * <http://oldblog.antirez.com/post/picol.html>
 *
 * Original Copyright notice:
 *
 * Tcl in ~ 500 lines of code.
 *
 * Copyright (c) 2007-2016, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE. */

#ifndef PICKLE_H
#define PICKLE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#define PICKLE_MAX_STRING    (512) /* Max string/Data structure size */
#define PICKLE_MAX_RECURSION (128) /* Recursion limit */
#define PICKLE_MAX_ARGS      (128) /* Maximum arguments to some internal functions */

typedef struct {
	void *(*malloc)  (void *arena, size_t bytes);
	void *(*realloc) (void *arena, void *ptr, size_t bytes);
	void  (*free)    (void *arena, void *ptr);
	void *arena;
} pickle_allocator_t; /* optional */

enum { PICKLE_OK, PICKLE_ERROR, PICKLE_RETURN, PICKLE_BREAK, PICKLE_CONTINUE };

struct pickle_interpreter;
typedef struct pickle_interpreter pickle_t;

typedef int (*pickle_command_func_t)(pickle_t *i, int argc, char **argv, void *privdata);

int pickle_new(pickle_t **i, const pickle_allocator_t *a); /* if(a == NULL) default allocator used */
int pickle_delete(pickle_t *i);
int pickle_eval(pickle_t *i, const char *t);
int pickle_register_command(pickle_t *i, const char *name, pickle_command_func_t f, void *privdata);

int pickle_error(pickle_t *i, const char *fmt, ...);
int pickle_error_out_of_memory(pickle_t *i); /* does not allocate */
int pickle_error_arity(pickle_t *i, int expected, int argc, char **argv);

int pickle_set_result_string(pickle_t *i, const char *s);
int pickle_set_result_integer(pickle_t *i, long result);
int pickle_get_result_string(pickle_t *i, const char **s);
int pickle_get_result_integer(pickle_t *i, long *val);

int pickle_set_var_string(pickle_t *i, const char *name, const char *val);
int pickle_set_var_integer(pickle_t *i, const char *name, long r);
int pickle_get_var_string(pickle_t *i, const char *name, const char **val);
int pickle_get_var_integer(pickle_t *i, const char *name, long *val);

int pickle_tests(void); /* returns: test passed || defined(NDEBUG) */

#ifdef __cplusplus
}
#endif
#endif
