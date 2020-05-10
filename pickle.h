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

#ifndef PICKLE_API
#define PICKLE_API /* Used to apply attributes to exported functions */
#endif

#ifndef ALLOCATOR_FN
#define ALLOCATOR_FN
typedef void *(*allocator_fn)(void *arena, void *ptr, size_t oldsz, size_t newsz);
#endif

struct pickle_interpreter;
typedef struct pickle_interpreter pickle_t;
typedef int (*pickle_command_func_t)(pickle_t *i, int argc, char **argv, void *privdata);

enum { PICKLE_ERROR = -1, PICKLE_OK, PICKLE_RETURN, PICKLE_BREAK, PICKLE_CONTINUE };

PICKLE_API int pickle_new(pickle_t **i, allocator_fn a, void *arena);
PICKLE_API int pickle_delete(pickle_t *i);
PICKLE_API int pickle_eval(pickle_t *i, const char *t);
PICKLE_API int pickle_register_command(pickle_t *i, const char *name, pickle_command_func_t f, void *privdata);
PICKLE_API int pickle_rename_command(pickle_t *i, const char *src, const char *dst); /* if 'dst' is "" then command is deleted */
PICKLE_API int pickle_version(unsigned long *version); /* version in x.y.z format, z = LSB. MSB = library info */

PICKLE_API int pickle_allocate(pickle_t *i, void **v, size_t size); /* zeroes allocated memory */
PICKLE_API int pickle_reallocate(pickle_t *i, void **v, const size_t size); /* frees v on failure */
PICKLE_API int pickle_free(pickle_t *i, void **v);
PICKLE_API int pickle_concatenate(pickle_t *i, int argc, char **argv, char **cat); /* returned in 'cat', caller frees */

PICKLE_API int pickle_set_result(pickle_t *i, const char *fmt, ...);
PICKLE_API int pickle_set_result_empty(pickle_t *i);
PICKLE_API int pickle_set_result_string(pickle_t *i, const char *s);
PICKLE_API int pickle_set_result_integer(pickle_t *i, long result);
PICKLE_API int pickle_get_result_string(pickle_t *i, const char **s);
PICKLE_API int pickle_get_result_integer(pickle_t *i, long *val);

PICKLE_API int pickle_set_result_error(pickle_t *i, const char *fmt, ...); /* always returns PICKLE_ERROR */
PICKLE_API int pickle_set_result_error_arity(pickle_t *i, int expected, int argc, char **argv); /* always returns PICKLE_ERROR */

PICKLE_API int pickle_set_var_string(pickle_t *i, const char *name, const char *val);
PICKLE_API int pickle_set_var_integer(pickle_t *i, const char *name, long r);
PICKLE_API int pickle_get_var_string(pickle_t *i, const char *name, const char **val);
PICKLE_API int pickle_get_var_integer(pickle_t *i, const char *name, long *val);

PICKLE_API int pickle_tests(allocator_fn fn, void *arena); /* returns: PICKLE_OK on success, PICKLE_ERROR on failure, PICKLE_OK if defined(NDEBUG) */

#ifdef __cplusplus
}
#endif
#endif
