/**@file pickle.h
 * @brief Pickle interpreter header, a TCL like language based on 'picol'.
 * BSD license: See <https://github.com/howerj/pickle/blob/master/LICENSE>
 * Copyright (c) 2007-2016, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2018-2020, Richard James Howe <howe.r.j.89@gmail.com> */

#ifndef PICKLE_H
#define PICKLE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#ifndef PICKLE_API
#define PICKLE_API /* Used to apply attributes to exported functions */
#endif

#ifndef ALLOCATOR_FN
#define ALLOCATOR_FN
typedef void *(*allocator_fn)(void *arena, void *ptr, size_t oldsz, size_t newsz);
#endif

struct pickle_interpreter;
typedef struct pickle_interpreter pickle_t;
typedef int (*pickle_func_t)(pickle_t *i, int argc, char **argv, void *privdata);

enum { PICKLE_ERROR = -1, PICKLE_OK, PICKLE_RETURN, PICKLE_BREAK, PICKLE_CONTINUE, };

PICKLE_API int pickle_new(pickle_t **i, allocator_fn a, void *arena);
PICKLE_API int pickle_delete(pickle_t *i);
PICKLE_API int pickle_eval(pickle_t *i, const char *t);
PICKLE_API int pickle_eval_args(pickle_t *i, int argc, char **argv);
PICKLE_API int pickle_command_register(pickle_t *i, const char *name, pickle_func_t f, void *privdata);
PICKLE_API int pickle_command_rename(pickle_t *i, const char *src, const char *dst);
PICKLE_API int pickle_allocator_get(pickle_t *i, allocator_fn *a, void **arena);
PICKLE_API int pickle_result_set(pickle_t *i, int ret, const char *fmt, ...);
PICKLE_API int pickle_result_get(pickle_t *i, const char **s);
PICKLE_API int pickle_var_set(pickle_t *i, const char *name, const char *val);
PICKLE_API int pickle_var_get(pickle_t *i, const char *name, const char **val);
PICKLE_API int pickle_tests(allocator_fn fn, void *arena);

#ifdef __cplusplus
}
#endif
#endif
