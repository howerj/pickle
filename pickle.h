/** <https://github.com/howerj/pickle>
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

typedef void *(*calloc_t)(void *arena,  size_t bytes);
typedef void *(*realloc_t)(void *arena, void *ptr, size_t bytes);
typedef void  (*free_t)(void *arena,    void *ptr);

typedef struct {
	calloc_t  calloc;
	realloc_t realloc;
	free_t    free;
	void     *arena;
} allocator_t;

struct pickle_command;
struct pickle_call_frame;

struct pickle_interpreter {
	allocator_t allocator;
	struct pickle_call_frame *callframe;
	struct pickle_command    *commands;
	char *result;
	int initialized;
	int level; /* Level of nesting */
};

typedef struct pickle_interpreter pickle_t;

typedef int (*pickle_command_func_t)(pickle_t *i, int argc, char **argv, void *privdata);

int pickle_register_command(pickle_t *i, const char *name, pickle_command_func_t f, void *privdata);
int pickle_eval(pickle_t *i, char *t);
int pickle_initialize(pickle_t *i, allocator_t *a); /* if(a == NULL) default allocator used */
int pickle_deinitialize(pickle_t *i);

int pickle_arity_error(pickle_t *i, const char *name); /* use within registered command if wrong number of args given */
char *pickle_set_result(pickle_t *i, const char *s);   /* set result within registered command */

#ifdef __cplusplus
}
#endif

#endif
