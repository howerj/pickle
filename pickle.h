/**@file pickle.h
 * @brief Pickle interpreter header, a TCL like language based on 'picol'.
 *
 * BSD license, See <https://github.com/howerj/pickle/blob/master/LICENSE>
 * or pickle.c for more information.
 *
 * Copyright (c) 2007-2016, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2018-2019, Richard James Howe <howe.r.j.89@gmail.com> */

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
	void *(*malloc)  (void *arena, size_t bytes);            /* malloc equivalent */
	void *(*realloc) (void *arena, void *ptr, size_t bytes); /* realloc equivalent */
	void  (*free)    (void *arena, void *ptr);               /* free equivalent */
	void *arena;  /* arena we are allocating in, if any */
} pickle_allocator_t; /* optional */

typedef struct {
	char *arg;   /* parsed argument */
	int error,   /* turn error reporting on/off */
	    index,   /* index into argument list */
	    option,  /* parsed option */
	    reset;   /* set to reset */
	char *place; /* internal use: scanner position */
	int  init;   /* internal use: initialized or not */
} pickle_getopt_t;   /* getopt clone */

struct pickle_interpreter;
typedef struct pickle_interpreter pickle_t;

typedef int (*pickle_command_func_t)(pickle_t *i, int argc, char **argv, void *privdata);

/* All the following functions return one of the pickle error statuses; PICKLE_OK,
 * PICKLE_ERROR, ...*/

enum { PICKLE_ERROR = -1, PICKLE_OK, PICKLE_RETURN, PICKLE_BREAK, PICKLE_CONTINUE };

int pickle_getopt(pickle_getopt_t *opt, int argc, char *const argv[], const char *fmt); /* PICKLE_RETURN when done */

int pickle_new(pickle_t **i, const pickle_allocator_t *a); /* if(a == NULL) default allocator used */
int pickle_delete(pickle_t *i);
int pickle_eval(pickle_t *i, const char *t);
int pickle_register_command(pickle_t *i, const char *name, pickle_command_func_t f, void *privdata);

int pickle_set_result(pickle_t *i, const char *fmt, ...);
int pickle_set_result_empty(pickle_t *i);
int pickle_set_result_error(pickle_t *i, const char *fmt, ...);
int pickle_set_result_error_arity(pickle_t *i, int expected, int argc, char **argv);
int pickle_set_result_error_memory(pickle_t *i);
int pickle_set_result_string(pickle_t *i, const char *s);
int pickle_set_result_integer(pickle_t *i, long result);
int pickle_get_result_string(pickle_t *i, const char **s);
int pickle_get_result_integer(pickle_t *i, long *val);

int pickle_set_var_string(pickle_t *i, const char *name, const char *val);
int pickle_set_var_integer(pickle_t *i, const char *name, long r);
int pickle_get_var_string(pickle_t *i, const char *name, const char **val);
int pickle_get_var_integer(pickle_t *i, const char *name, long *val);

unsigned long pickle_hash_string(const char *s, size_t len);
int pickle_strcmp(const char *a, const char *b);

int pickle_tests(void); /* returns: test passed || defined(NDEBUG) */

int pickle_command_string(pickle_t *i, const int argc, char **argv, void *pd);

#ifdef __cplusplus
}
#endif
#endif
