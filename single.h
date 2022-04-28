/* Single File TCL like interpreter for embedded use.
 *
 * You should only rely on symbols that begin with "PICKLE_" or
 * "pickle_", or the allocator function, everything else is internal
 * and subject to change. Internally the prefixes "picol_", "PICOL_",
 * and "PCL_" are used to avoid namespaces classes in code that
 * incorporates this header. The header is not (currently) C++
 * safe.
 *
 * TODO: Move readme.md to here(?), C++ compilation, rename variables
 * to prevent a clash, inline documentation for everything including
 * design decisions, mention recompilation and execution of faster
 * byte code using the high byte of characters, change makefile so
 * it can compile shared libraries and install from this header. Add
 * makefile instructions to this file. */
#ifndef PICKLE_H
#define PICKLE_H
#ifdef __cplusplus
extern "C" {
#endif

#define PICKLE_VERSION 0x060000ul
#define PICKLE_AUTHOR "Richard James Howe"
#define PICKLE_EMAIL "howe.r.j.89@gmail.com"
#define PICKLE_REPO "https://github.com/howerj/pickle"
#define PICKLE_LICENSE "\
Copyright (c) 2007-2016 Salvatore Sanfilippo / 2018-2022 Richard James Howe\n\
All rights reserved.\n\
\n\
Redistribution and use in source and binary forms, with or without\n\
modification, are permitted provided that the following conditions are met:\n\
\n\
  * Redistributions of source code must retain the above copyright notice,\n\
    this list of conditions and the following disclaimer.\n\
  * Redistributions in binary form must reproduce the above copyright\n\
    notice, this list of conditions and the following disclaimer in the\n\
    documentation and/or other materials provided with the distribution.\n\
\n\
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\"\n\
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE\n\
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE\n\
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE\n\
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR\n\
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF\n\
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS\n\
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN\n\
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)\n\
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE\n\
POSSIBILITY OF SUCH DAMAGE."

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

#if defined(PICKLE_DEFINE_IMPLEMENTATION)

#include <assert.h>  /* !defined(NDEBUG): assert */
#include <ctype.h>   /* toupper, tolower, isalnum, isalpha, ... */
#include <stdint.h>  /* intptr_t */
#include <limits.h>  /* CHAR_BIT, LONG_MAX, LONG_MIN */
#include <stdarg.h>  /* va_list, va_start, va_end */
#include <stdio.h>   /* vsnprintf see <https://github.com/mpaland/printf> if you lack this */
#include <string.h>  /* memset, memchr, strstr, strcmp, strncmp, strcpy, strlen, strchr */

#define PICOL_SMALL_RESULT_BUF_SZ     (96)
#define PICOL_PRINT_NUMBER_BUF_SZ     (64 /* base 2 */ + 1 /* '-'/'+' */ + 1 /* NUL */)
#define PICOL_UNUSED(X)                     ((void)(X))
#define PICOL_BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))
#define picolCheck(EXP)               assert(EXP)
#define picolImplies(P, Q)            picolImplication(!!(P), !!(Q)) /* material implication, immaterial if NDEBUG defined */
#define picolMutual(P, Q)             (picolImplies((P), (Q)), picolImplies((Q), (P)))
#define picolMemberSize(TYPE, MEMBER) (sizeof(((TYPE *)0)->MEMBER)) /* apparently fine */

#ifndef MIN
#define MIN(X, Y)                     ((X) > (Y) ? (Y) : (X))
#endif
#ifndef MAX
#define MAX(X, Y)                     ((X) > (Y) ? (X) : (Y))
#endif

#ifndef PICOL_PREPACK
#define PICOL_PREPACK
#endif

#ifndef PICOL_POSTPACK
#define PICOL_POSTPACK
#endif

#ifdef NDEBUG
#define PICOL_DEBUGGING         (0)
#else
#define PICOL_DEBUGGING         (1)
#endif

#ifndef PICOL_DEFINE_TESTS
#define PICOL_DEFINE_TESTS      (1)
#endif

#ifndef PICOL_DEFINE_MATHS
#define PICOL_DEFINE_MATHS      (1)
#endif

#ifndef PICOL_DEFINE_STRING
#define PICOL_DEFINE_STRING     (1)
#endif

#ifndef PICOL_DEFINE_LIST
#define PICOL_DEFINE_LIST       (1)
#endif

#ifndef PICOL_DEFINE_REGEX
#define PICOL_DEFINE_REGEX      (1)
#endif

#ifndef PICOL_DEFINE_HELP
#define PICOL_DEFINE_HELP       (0)
#endif

#ifndef PICKLE_MAX_RECURSION
#define PICKLE_MAX_RECURSION (128) /* Recursion limit */
#endif

#ifndef PICOL_USE_MAX_STRING
#define PICOL_USE_MAX_STRING    (0)
#endif

#ifndef PICKLE_MAX_STRING
#define PICKLE_MAX_STRING (768) /* Max string/Data structure size, if PICOL_USE_MAX_STRING != 0 */
#endif

#ifndef PICOL_STRICT_NUMERIC_CONVERSION
#define PICOL_STRICT_NUMERIC_CONVERSION (1)
#endif

#if PICOL_DEFINE_HELP == 1
#define PICOL_ARITY(COMP, MSG) if ((COMP)) { return picolSetResultArgError(i, __LINE__, #COMP, (MSG), argc, argv); }
#else
#define PICOL_ARITY(COMP, MSG) if ((COMP)) { return picolSetResultArgError(i, __LINE__, "", "", argc, argv); }
#endif

#define ok(i, ...)       pickle_result_set(i, PICKLE_OK,    __VA_ARGS__)
#ifndef PICKLE_LINE_NUMBER
#define error(i, ...)    pickle_result_set(i, PICKLE_ERROR,    __VA_ARGS__)
#else
#define PICOL_STRINGIFY(x)     #x
#define PICOL_TOSTRING(x)      PICOL_STRINGIFY(x)
#define error(i, ...)    pickle_result_set(i, PICKLE_ERROR, PICOL_TOSTRING(__LINE__) ": " __VA_ARGS__)
#endif

enum { PCL_ESC, PCL_STR, PCL_CMD, PCL_VAR, PCL_SEP, PCL_EOL, PCL_EOF, };

typedef PICOL_PREPACK struct {
	unsigned nocommands :1, /* turn off commands */
	         noescape   :1, /* turn off escape sequences */
	         novars     :1, /* turn off variables */
	         noeval     :1; /* turn off command evaluation */
} PICOL_POSTPACK pickle_parser_opts_t; /* options for parser/evaluator */

typedef PICOL_PREPACK struct {
	const char *text;          /* the program */
	const char *p;             /* current text position */
	const char *start;         /* token start */
	const char *end;           /* token end */
	int len;                   /* remaining length */
	int type;                  /* token type, PCL_... */
	pickle_parser_opts_t o;    /* parser options */
	unsigned inside_quote: 1;  /* true if inside " " */
} PICOL_POSTPACK picol_parser_t;  /* Parsing structure */

typedef PICOL_PREPACK struct {
	char buf[PICOL_SMALL_RESULT_BUF_SZ]; /* small temporary buffer */
	char *p;                             /* either points to buf or is allocated */
	size_t length;                       /* length of 'p' */
} PICOL_POSTPACK picol_stack_or_heap_t;     /* allocate on stack, or move to heap, depending on needs */

enum { PICOL_PV_STRING, PICOL_PV_SMALL_STRING, PICOL_PV_LINK, };

typedef union {
	char *ptr,  /* pointer to string that has spilled over 'small' in size */
	     small[sizeof(char*)]; /* string small enough to be stored in a pointer (including NUL terminator)*/
} picol_compact_string_t; /* either a pointer to a string, or a string stored in a pointer */

PICOL_PREPACK struct pickle_var { /* strings are stored as either pointers, or as 'small' strings */
	picol_compact_string_t name; /* name of variable */
	union {
		picol_compact_string_t val; /* value */
		struct pickle_var *link;    /* link to another variable */
	} data;
	struct pickle_var *next; /* next variable in list of variables */

	unsigned type      : 2; /* type of data; string (pointer/small), or link (NB. Could add number type) */
	unsigned smallname : 1; /* if true, name is stored as small string */
} PICOL_POSTPACK;

PICOL_PREPACK struct pickle_command {
	char *name;                  /* name of function, it would be nice if this was a 'picol_compact_string_t' */
	pickle_func_t func;          /* pointer to function that implements this command */
	struct pickle_command *next; /* next command in list (chained hash table) */
	void *privdata;              /* (optional) private data for function */
} PICOL_POSTPACK;

PICOL_PREPACK struct pickle_call_frame {        /* A call frame, organized as a linked list */
	struct pickle_var *vars;          /* first variable in linked list of variables */
	struct pickle_call_frame *parent; /* parent is NULL at top level */
} PICOL_POSTPACK;

PICOL_PREPACK struct pickle_interpreter { /* The Pickle Interpreter! */
	char result_buf[PICOL_SMALL_RESULT_BUF_SZ];/* store small results here without allocating */
	allocator_fn allocator;              /* custom allocator, if desired */
	void *arena;                         /* arena for custom allocator, if needed */
	const char *result;                  /* result of an evaluation */
	struct pickle_call_frame *callframe; /* call stack */
	struct pickle_command **table;       /* hash table */
	long length;                         /* buckets in hash table */
	long cmdcount;                       /* total number of commands invoked in this interpreter */
	int level, evals;                    /* level of functional call and evaluation nesting */
	unsigned initialized    :1;          /* if true, interpreter is initialized and ready to use */
	unsigned static_result  :1;          /* internal use only: if true, result should not be freed */
	unsigned inside_uplevel :1;          /* true if executing inside an uplevel command */
	unsigned inside_unknown :1;          /* true if executing inside the 'unknown' proc */
	unsigned fatal          :1;          /* true if a fatal error has occurred */
	unsigned inside_trace   :1;          /* true if we are inside the trace function */
	unsigned trace          :1;          /* true if tracing is on */
} PICOL_POSTPACK;

typedef PICOL_PREPACK struct {
	const char *start,  /* start of match, NULL if no match */
	           *end;    /* end of match, NULL if no match */
	int max;            /* maximum recursion depth (0 = unlimited) */
	unsigned type   :2, /* select regex type; lazy, greedy or possessive */
	         nocase :1; /* ignore case when matching */
} PICOL_POSTPACK pickle_regex_t;  /* used to specify a regex and return start/end of match */

typedef struct PICOL_PREPACK { int argc; char **argv; } PICOL_POSTPACK picol_args_t;

typedef struct pickle_var pickle_var_t;
typedef struct pickle_call_frame pickle_call_frame_t;
typedef struct pickle_command pickle_command_t;

typedef long picol_number_t;
typedef unsigned long picol_unumber_t;
#define PICOL_NUMBER_MIN (LONG_MIN)
#define PICOL_NUMBER_MAX (LONG_MAX)

static const char  picol_string_empty[]     = "";              /* Space saving measure */
static const char  picol_string_oom[]       = "Error out of memory"; /* Cannot allocate this, obviously */
static const char *picol_string_whitespace = " \t\n\r\v";
static const char *picol_string_digits      = "0123456789abcdefghijklmnopqrstuvwxyz";

static int picolForceResult(pickle_t *i, const char *result, const int is_static);
static int picolSetResultString(pickle_t *i, const char *s);

static inline void picolImplication(const int p, const int q) {
	PICOL_UNUSED(p); PICOL_UNUSED(q); /* warning suppression if NDEBUG defined */
	picolCheck((!p) || q);
}

static inline void static_assertions(void) { /* A neat place to put these */
	PICOL_BUILD_BUG_ON(PICKLE_MAX_STRING    < 128);
	PICOL_BUILD_BUG_ON(sizeof (struct pickle_interpreter) > PICKLE_MAX_STRING);
	PICOL_BUILD_BUG_ON(sizeof (picol_string_oom) > PICOL_SMALL_RESULT_BUF_SZ);
	PICOL_BUILD_BUG_ON(PICKLE_MAX_RECURSION < 8);
	PICOL_BUILD_BUG_ON(PICKLE_OK    !=  0);
	PICOL_BUILD_BUG_ON(PICKLE_ERROR != -1);
}

static inline void *picolLocateByte(const void *m, const int c, const size_t n) {
	picolCheck(m);
	return memchr(m, c, n);
}

static inline void picolPre(pickle_t *i) { /* assert API pre-conditions */
	PICOL_UNUSED(i); /* warning suppression when NDEBUG defined */
	picolCheck(i);
	picolCheck(i->initialized);
	picolCheck(i->result);
	picolCheck(i->length > 0);
	picolCheck(!!picolLocateByte(i->result_buf, 0, sizeof i->result_buf));
	picolCheck(i->level >= 0);
}

static inline int picolPost(pickle_t *i, const int r) { /* assert API post-conditions */
	picolPre(i);
	picolCheck(r >= PICKLE_ERROR && r <= PICKLE_CONTINUE);
	return i->fatal ? PICKLE_ERROR : r;
}

static inline int picolCompare(const char *a, const char *b) {
	picolCheck(a);
	picolCheck(b);
	if (PICOL_USE_MAX_STRING)
		return strncmp(a, b, PICKLE_MAX_STRING);
	return strcmp(a, b);
}

static inline size_t picolStrnlen(const char *s, size_t length) {
	picolCheck(s);
	size_t r = 0;
	for (r = 0; r < length && *s; s++, r++)
		;
	return r;
}

static inline size_t picolStrlen(const char *s) {
	picolCheck(s);
	return PICOL_USE_MAX_STRING ? picolStrnlen(s, PICKLE_MAX_STRING) : strlen(s);
}

static inline void *picolMove(void *dst, const void *src, const size_t length) {
	picolCheck(dst);
	picolCheck(src);
	return memmove(dst, src, length);
}

static inline void *picolZero(void *m, const size_t length) {
	picolCheck(m);
	if (length == 0)
		return m;
	return memset(m, 0, length);
}

static inline char *picolCopy(char *dst, const char *src) {
	picolCheck(src);
	picolCheck(dst);
	return strcpy(dst, src);
}

static inline char *picolFind(const char *haystack, const char *needle) {
	picolCheck(haystack);
	picolCheck(needle);
	return strstr(haystack, needle);
}

static inline const char *picolRFind(const char *haystack, const char *needle) {
	picolCheck(haystack);
	picolCheck(needle);
	const size_t hay = strlen(haystack);
	for (size_t i = 0; i < hay; i++)
		if (picolFind(&haystack[hay - i - 1], needle))
			return &haystack[hay - i - 1];
	return NULL;
}

static inline char *picolLocateChar(const char *s, const int c) {
	picolCheck(s);
	return strchr(s, c);
}

static void *picolMalloc(pickle_t *i, size_t size) {
	picolCheck(i);
	picolCheck(size > 0); /* we do not allocate any zero length objects in this code */
	if (i->fatal || (PICOL_USE_MAX_STRING && size > PICKLE_MAX_STRING))
		goto fail;
	void *r = i->allocator(i->arena, NULL, 0, size);
	if (!r && size)
		goto fail;
	return r;
fail:
	i->fatal = 1;
	(void)picolForceResult(i, picol_string_oom, 1);
	return NULL;
}

static void *picolRealloc(pickle_t *i, void *p, size_t size) {
	picolCheck(i);
	if (i->fatal || (PICOL_USE_MAX_STRING && size > PICKLE_MAX_STRING))
		goto fail;
	void *r = i->allocator(i->arena, p, 0, size);
	if (!r && size)
		goto fail;
	return r;
fail:
	i->fatal = 1;
	(void)picolForceResult(i, picol_string_oom, 1); /* does not allocate, may free */
	return NULL;
}

static int picolFree(pickle_t *i, void *p) {
	picolCheck(i);
	picolCheck(i->allocator);
	const void *r = i->allocator(i->arena, p, 0, 0);
	if (r != NULL) {
		static const char msg[] = "Error free";
		PICOL_BUILD_BUG_ON(sizeof(msg) > PICOL_SMALL_RESULT_BUF_SZ);
		i->fatal = 1;
		(void)picolSetResultString(i, msg);
		return PICKLE_ERROR;
	}
	return PICKLE_OK;
}

static inline int picolIsBaseValid(const int base) {
	return base >= 2 && base <= 36; /* Base '0' is not a special case */
}

static inline int picolDigit(const int digit) {
	const char *found = picolLocateChar(picol_string_digits, tolower(digit));
	return found ? (int)(found - picol_string_digits) : -1;
}

static inline int picolIsDigit(const int digit, const int base) {
	picolCheck(picolIsBaseValid(base));
	const int r = picolDigit(digit);
	return r < base ? r : -1;
}

static int picolConvertBaseNNumber(pickle_t *i, const char *s, picol_number_t *out, int base) {
	picolCheck(i);
	picolCheck(i->initialized);
	picolCheck(s);
	picolCheck(picolIsBaseValid(base));
	static const size_t max = MIN(PICOL_PRINT_NUMBER_BUF_SZ, PICKLE_MAX_STRING);
	picol_number_t result = 0;
	int ch = s[0];
	const int negate = ch == '-';
	const int prefix = negate || s[0] == '+';
	*out = 0;
	if (PICOL_STRICT_NUMERIC_CONVERSION && prefix && !s[prefix])
		return error(i, "Error number");
	if (PICOL_STRICT_NUMERIC_CONVERSION && !ch)
		return error(i, "Error number");
	for (size_t j = prefix; j < max && (ch = s[j]); j++) {
		const picol_number_t digit = picolIsDigit(ch, base);
		if (digit < 0)
			break;
		result = digit + (result * (picol_number_t)base);
	}
	if (PICOL_STRICT_NUMERIC_CONVERSION && ch)
		return error(i, "Error number");
	*out = negate ? -result : result;
	return PICKLE_OK;
}

/* NB. We could make a version of this that could also process 'end' and
 * 'end-#' numbers for indexing, it would require a length however. This
 * would be useful for commands like 'string index' and some of the list
 * manipulation commands. */
static int picolStringToNumber(pickle_t *i, const char *s, picol_number_t *out) {
	picolCheck(i);
	picolCheck(s);
	picolCheck(out);
	return picolConvertBaseNNumber(i, s, out, 10);
}

static inline int picolCompareCaseInsensitive(const char *a, const char *b) {
	picolCheck(a);
	picolCheck(b);
	for (size_t i = 0; ; i++) {
		const int ach = tolower(a[i]);
		const int bch = tolower(b[i]);
		const int diff = ach - bch;
		if (!ach || diff)
			return diff;
	}
	return 0;
}

static inline int picolLogarithm(picol_number_t a, const picol_number_t b, picol_number_t *c) {
	picolCheck(c);
	picol_number_t r = -1;
	*c = r;
	if (a <= 0 || b < 2)
		return PICKLE_ERROR;
	do r++; while (a /= b);
	*c = r;
	return PICKLE_OK;
}

static inline int picolPower(picol_number_t base, picol_number_t exp, picol_number_t *r) {
	picolCheck(r);
	picol_number_t result = 1, negative = 0;
	*r = 0;
	if (exp < 0)
		return PICKLE_ERROR;
	if (base < 0) {
		base = -base;
		negative = exp & 1;
	}
	for (;;) {
		if (exp & 1)
			result *= base;
		exp /= 2;
		if (!exp)
			break;
		base *= base;
	}
	*r = negative ? -result : result;
	return PICKLE_OK;
}

/* This is may seem like an odd function, say for small allocation we want to
 * keep them on the stack, but move them to the heap when they get too big, we can use
 * the 'picolStackOrHeapAlloc'/'picolStackOrHeapFree' functions to manage this. */
static int picolStackOrHeapAlloc(pickle_t *i, picol_stack_or_heap_t *s, size_t needed) {
	picolCheck(i);
	picolCheck(s);

	if (s->p == NULL) { /* take care of initialization */
		s->p      = s->buf;
		s->length = sizeof (s->buf);
		picolZero(s->buf, sizeof (s->buf));
	}

	if (needed <= s->length)
		return PICKLE_OK;
	if (PICOL_USE_MAX_STRING && needed > PICKLE_MAX_STRING)
		return PICKLE_ERROR;
	if (s->p == s->buf) {
		if (!(s->p = picolMalloc(i, needed)))
			return PICKLE_ERROR;
		s->length = needed;
		picolMove(s->p, s->buf, sizeof (s->buf));
		return PICKLE_OK;
	}
	char *old = s->p;
	if ((s->p = picolRealloc(i, s->p, needed)) == NULL) {
		(void)picolFree(i, old);
		return PICKLE_ERROR;
	}
	s->length = needed;
	return PICKLE_OK;
}

static int picolStackOrHeapFree(pickle_t *i, picol_stack_or_heap_t *s) {
	picolCheck(i);
	picolCheck(s);
	if (s->p != s->buf) {
		const int r = picolFree(i, s->p);
		s->p = NULL; /* prevent double free */
		s->length = 0;
		return r;
	}
	return PICKLE_OK; /* pointer == buffer, no need to free */
}

static int picolOnHeap(pickle_t *i, picol_stack_or_heap_t *s) {
	picolCheck(i); PICOL_UNUSED(i);
	picolCheck(s);
	return s->p && s->p != s->buf;
}

static char *picolStrdup(pickle_t *i, const char *s) {
	picolCheck(i);
	picolCheck(s);
	const size_t l = picolStrlen(s);
	char *r = picolMalloc(i, l + 1);
	return r ? picolMove(r, s, l + 1) : r;
}

static inline unsigned long picolHashString(const char *s) { /* DJB2 Hash, <http://www.cse.yorku.ca/~oz/hash.html> */
	picolCheck(s);
	unsigned long h = 5381, ch = 0; /* NB. strictly speaking 'uint32_t' should be used here */
	for (size_t i = 0; (ch = s[i]); i++) {
		picolImplies(PICOL_USE_MAX_STRING, i < PICKLE_MAX_STRING);
		h = ((h << 5) + h) + ch;
	}
	return h;
}

static inline pickle_command_t *picolGetCommand(pickle_t *i, const char *s) {
	picolCheck(s);
	picolCheck(i);
	pickle_command_t *np = NULL;
	for (np = i->table[picolHashString(s) % i->length]; np != NULL; np = np->next)
		if (!picolCompare(s, np->name))
			return np; /* found */
	return NULL; /* not found */
}

static int picolFreeResult(pickle_t *i) {
	picolCheck(i);
	return i->static_result ? PICKLE_OK : picolFree(i, (char*)i->result);
}

static int picolSetResultString(pickle_t *i, const char *s) {
	picolCheck(s);
	int is_static = 1;
	char *r = i->result_buf;
	const size_t sl = picolStrlen(s) + 1;
	if (sizeof(i->result_buf) < sl) {
		is_static = 0;
		r = picolMalloc(i, sl);
		if (!r)
			return PICKLE_ERROR;
	}
	picolMove(r, s, sl);
	const int fr = picolFreeResult(i);
	i->static_result = is_static;
	i->result = r;
	return fr;
}

static int picolForceResult(pickle_t *i, const char *result, const int is_static) {
	picolCheck(i);
	picolCheck(result);
	int r = picolFreeResult(i);
	i->static_result = is_static;
	i->result = result;
	return r;
}

static int picolSetResultEmpty(pickle_t *i) {
	picolCheck(i);
	return picolForceResult(i, picol_string_empty, 1);
}

static int picolSetResultArgError(pickle_t *i, const unsigned line, const char *comp, const char *help, const int argc, char **argv);
static int picolCommandCallProc(pickle_t *i, const int argc, char **argv, void *pd);

static int picolIsDefinedProc(pickle_func_t func) {
	return func == picolCommandCallProc;
}

/* <https://stackoverflow.com/questions/4384359/> */
static int picolRegisterCommand(pickle_t *i, const char *name, pickle_func_t func, void *privdata) {
	picolCheck(i);
	picolCheck(name);
	picolCheck(func);
	pickle_command_t *np = picolGetCommand(i, name);
	if (np) {
		if (picolIsDefinedProc(func)) {
			char **procdata = privdata;
			(void)picolFree(i, procdata[0]);
			(void)picolFree(i, procdata[1]);
			(void)picolFree(i, procdata);
		}
		return error(i, "Error option %s", name);
	}
	np = picolMalloc(i, sizeof(*np));
	if (np == NULL || (np->name = picolStrdup(i, name)) == NULL) {
		(void)picolFree(i, np);
		return PICKLE_ERROR;
	}
	const unsigned long hashval = picolHashString(name) % i->length;
	np->next = i->table[hashval];
	i->table[hashval] = np;
	np->func = func;
	np->privdata = privdata;
	return PICKLE_OK;
}

static int picolFreeCmd(pickle_t *i, pickle_command_t *p);

static int picolUnsetCommand(pickle_t *i, const char *name) {
	picolCheck(i);
	picolCheck(name);
	pickle_command_t **p = &i->table[picolHashString(name) % i->length];
	pickle_command_t *c = *p;
	for (; c; c = c->next) {
		if (!picolCompare(c->name, name)) {
			*p = c->next;
			return picolFreeCmd(i, c);
		}
		p = &c->next;
	}
	return error(i, "Error variable %s", name);
}

static int picolAdvance(picol_parser_t *p) {
	picolCheck(p);
	if (p->len <= 0)
		return PICKLE_ERROR;
	if (p->len && !(*p->p))
		return PICKLE_ERROR;
	p->p++;
	p->len--;
	if (p->len && !(*p->p))
		return PICKLE_ERROR;
	return PICKLE_OK;
}

static inline void picolParserInitialize(picol_parser_t *p, pickle_parser_opts_t *o, const char *text) {
	/* NB. picolCheck(o || !o); */
	picolCheck(p);
	picolCheck(text);
	picolZero(p, sizeof *p);
	p->text = text;
	p->p    = text;
	p->len  = strlen(text); /* unbounded! */
	p->type = PCL_EOL;
	p->o    = o ? *o : p->o;
}

static inline int picolIsSpaceChar(const int ch) {
	return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static inline int picolParseSep(picol_parser_t *p) {
	picolCheck(p);
	p->start = p->p;
	while (*p->p == ' ' || *p->p == '\t')
		if (picolAdvance(p) != PICKLE_OK)
			return PICKLE_ERROR;
	p->end  = p->p - 1;
	p->type = PCL_SEP;
	return PICKLE_OK;
}

static inline int picolParseEol(picol_parser_t *p) {
	picolCheck(p);
	p->start = p->p;
	while (picolIsSpaceChar(*p->p) || *p->p == ';')
		if (picolAdvance(p) != PICKLE_OK)
			return PICKLE_ERROR;
	p->end  = p->p - 1;
	p->type = PCL_EOL;
	return PICKLE_OK;
}

static inline int picolParseCommand(picol_parser_t *p) {
	picolCheck(p);
	if (picolAdvance(p) != PICKLE_OK)
		return PICKLE_ERROR;
	p->start = p->p;
	for (int level = 1, blevel = 0; p->len;) {
		if (*p->p == '[' && blevel == 0) {
			level++;
		} else if (*p->p == ']' && blevel == 0) {
			if (!--level)
				break;
		} else if (*p->p == '\\') {
			if (picolAdvance(p) != PICKLE_OK)
				return PICKLE_ERROR;
		} else if (*p->p == '{') {
			blevel++;
		} else if (*p->p == '}') {
			if (blevel != 0)
				blevel--;
		}
		if (picolAdvance(p) != PICKLE_OK)
			return PICKLE_ERROR;
	}
	if (*p->p != ']')
		return PICKLE_ERROR;
	p->end  = p->p - 1;
	p->type = PCL_CMD;
	if (picolAdvance(p) != PICKLE_OK)
		return PICKLE_ERROR;
	return PICKLE_OK;
}

static inline int picolIsVarChar(const int ch) {
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
}

static inline int picolParseVar(picol_parser_t *p) {
	picolCheck(p);
	int br = 0;
	if (picolAdvance(p) != PICKLE_OK) /* skip the $ */
		return PICKLE_ERROR;
	if (*p->p == '{') {
		if (picolAdvance(p) != PICKLE_OK)
			return PICKLE_ERROR;
		br = 1;
	}
	p->start = p->p;
	for (;picolIsVarChar(*p->p);)
	       	if (picolAdvance(p) != PICKLE_OK)
			return PICKLE_ERROR;
	p->end = p->p - 1;
	if (br) {
		if (*p->p != '}')
			return PICKLE_ERROR;
		if (picolAdvance(p) != PICKLE_OK)
			return PICKLE_ERROR;
	}
	if (!br && p->start == p->p) { /* It's just a single char string "$" */
		p->start = p->p - 1;
		p->type  = PCL_STR;
	} else {
		p->type = PCL_VAR;
	}
	return PICKLE_OK;
}

static inline int picolParseBrace(picol_parser_t *p) {
	picolCheck(p);
	if (picolAdvance(p) != PICKLE_OK)
		return PICKLE_ERROR;
	p->start = p->p;
	for (int level = 1;;) {
		if (p->len >= 2 && *p->p == '\\') {
			if (picolAdvance(p) != PICKLE_OK)
				return PICKLE_ERROR;
		} else if (p->len == 0) {
			return PICKLE_ERROR;
		} else if (*p->p == '}') {
			level--;
			if (level == 0) {
				p->end  = p->p - 1;
				p->type = PCL_STR;
				if (p->len)
					return picolAdvance(p); /* Skip final closed brace */
				return PICKLE_OK;
			}
		} else if (*p->p == '{') {
			level++;
		}
		if (picolAdvance(p) != PICKLE_OK)
			return PICKLE_ERROR;
	}
	return PICKLE_OK; /* unreached */
}

static int picolParseString(picol_parser_t *p) {
	picolCheck(p);
	const int newword = (p->type == PCL_SEP || p->type == PCL_EOL || p->type == PCL_STR);
	if (newword && *p->p == '{') {
		return picolParseBrace(p);
	} else if (newword && *p->p == '"') {
		p->inside_quote = 1;
		if (picolAdvance(p) != PICKLE_OK)
			return PICKLE_ERROR;
	}
	p->start = p->p;
	for (;p->len;) {
		switch (*p->p) {
		case '\\':
			if (p->o.noescape)
				break;
			if (p->len >= 2)
				if (picolAdvance(p) != PICKLE_OK)
					return PICKLE_ERROR;
			break;
		case '$':
			if (p->o.novars)
				break;
			p->end  = p->p - 1;
			p->type = PCL_ESC;
			return PICKLE_OK;
		case '[':
			if (p->o.nocommands)
				break;
			p->end  = p->p - 1;
			p->type = PCL_ESC;
			return PICKLE_OK;
		case '\n': case ' ': case '\t': case '\r': case ';':
			if (!p->inside_quote) {
				p->end  = p->p - 1;
				p->type = PCL_ESC;
				return PICKLE_OK;
			}
			break;
		case '"':
			if (p->inside_quote) {
				p->end  = p->p - 1;
				p->type = PCL_ESC;
				p->inside_quote = 0;
				return picolAdvance(p);
			}
			break;
		}
		if (picolAdvance(p) != PICKLE_OK)
			return PICKLE_ERROR;
	}
	if (p->inside_quote)
		return PICKLE_ERROR;
	p->end = p->p - 1;
	p->type = PCL_ESC;
	return PICKLE_OK;
}

static inline int picolParseComment(picol_parser_t *p) {
	picolCheck(p);
	while (p->len && *p->p != '\n') {
		if (*p->p == '\\' && p->p[1] == '\n') /* Unix line endings only */
			if (picolAdvance(p) != PICKLE_OK)
				return PICKLE_ERROR;
		if (picolAdvance(p) != PICKLE_OK)
			return PICKLE_ERROR;
	}
	return PICKLE_OK;
}

static int picolGetToken(picol_parser_t *p) {
	picolCheck(p);
	for (;p->len;) {
		switch (*p->p) {
		case ' ': case '\t':
			if (p->inside_quote)
				return picolParseString(p);
			return picolParseSep(p);
		case '\r': case '\n': case ';':
			if (p->inside_quote)
				return picolParseString(p);
			return picolParseEol(p);
		case '[': {
			const int r = picolParseCommand(p);
			if (r == PICKLE_OK && p->o.nocommands && p->type == PCL_CMD) {
				p->start--, p->end++;
				p->type = PCL_STR;
				picolCheck(*p->start == '[' && *p->end == ']');
			}
			return r;
		}
		case '$':
			return p->o.novars ? picolParseString(p) : picolParseVar(p);
		case '#':
			if (p->type == PCL_EOL) {
				if (picolParseComment(p) != PICKLE_OK)
					return PICKLE_ERROR;
				continue;
			}
			return picolParseString(p);
		default:
			return picolParseString(p);
		}
	}
	if (p->type != PCL_EOL && p->type != PCL_EOF)
		p->type = PCL_EOL;
	else
		p->type = PCL_EOF;
	return PICKLE_OK;
}

static pickle_var_t *picolGetVar(pickle_t *i, const char *name, int link) {
	picolCheck(i);
	picolCheck(name);
	pickle_var_t *v = i->callframe->vars;
	while (v) {
		const char *n = v->smallname ? &v->name.small[0] : v->name.ptr;
		picolCheck(n);
		if (!picolCompare(n, name)) {
			if (link)
				while (v->type == PICOL_PV_LINK) { /* NB. Could resolve link at creation? */
					picolCheck(v != v->data.link); /* Cycle? */
					v = v->data.link;
				}
			picolImplies(v->type == PICOL_PV_STRING, v->data.val.ptr);
			return v;
		}
		/* See <https://en.wikipedia.org/wiki/Cycle_detection>,
		 * <https://stackoverflow.com/questions/2663115>, or "Floyd's
		 * cycle-finding algorithm" for proper loop detection */
		picolCheck(v != v->next); /* Cycle? */
		v = v->next;
	}
	return NULL;
}

static int picolFreeVarName(pickle_t *i, pickle_var_t *v) {
	picolCheck(i);
	picolCheck(v);
	return v->smallname ? PICKLE_OK : picolFree(i, v->name.ptr);
}

static int picolFreeVarVal(pickle_t *i, pickle_var_t *v) {
	picolCheck(i);
	picolCheck(v);
	return v->type == PICOL_PV_STRING ? picolFree(i, v->data.val.ptr) : PICKLE_OK;
}

/* return: non-zero if and only if val fits in a small string */
static inline int picolIsSmallString(const char *val) {
	picolCheck(val);
	return !!picolLocateByte(val, 0, picolMemberSize(picol_compact_string_t, small));
}

static int picolSetVarString(pickle_t *i, pickle_var_t *v, const char *val) {
	picolCheck(i);
	picolCheck(v);
	picolCheck(val);
	if (picolIsSmallString(val)) {
		v->type = PICOL_PV_SMALL_STRING;
		picolCopy(v->data.val.small, val);
		return PICKLE_OK;
	}
	v->type = PICOL_PV_STRING;
	return (v->data.val.ptr = picolStrdup(i, val)) ? PICKLE_OK : PICKLE_ERROR;
}

static inline int picolSetVarName(pickle_t *i, pickle_var_t *v, const char *name) {
	picolCheck(i);
	picolCheck(v);
	picolCheck(name);
	if (picolIsSmallString(name)) {
		v->smallname = 1;
		picolCopy(v->name.small, name);
		return PICKLE_OK;
	}
	v->smallname = 0;
	return (v->name.ptr = picolStrdup(i, name)) ? PICKLE_OK : PICKLE_ERROR;
}

static const char *picolGetVarVal(pickle_var_t *v) {
	picolCheck(v);
	picolCheck((v->type == PICOL_PV_SMALL_STRING) || (v->type == PICOL_PV_STRING));
	switch (v->type) {
	case PICOL_PV_SMALL_STRING: return v->data.val.small;
	case PICOL_PV_STRING:       return v->data.val.ptr;
	}
	return NULL;
}

static inline void picolSwapString(char **a, char **b) {
	picolCheck(a);
	picolCheck(b);
	char *t = *a;
	*a = *b;
	*b = t;
}

static inline void picolSwapChar(char * const a, char * const b) {
	picolCheck(a);
	picolCheck(b);
	const char t = *a;
	*a = *b;
	*b = t;
}

static inline char *picolReverse(char *s, size_t length) { /* Modifies Argument */
	picolCheck(s);
	for (size_t i = 0; i < (length/2); i++)
		picolSwapChar(&s[i], &s[(length - i) - 1]);
	return s;
}

static int picolNumberToString(char buf[/*static*/ 64/*base 2*/ + 1/*'+'/'-'*/ + 1/*NUL*/], picol_number_t in, int base) {
	picolCheck(buf);
	int negate = 0;
	size_t i = 0;
	picol_unumber_t dv = in;
	if (!picolIsBaseValid(base))
		return PICKLE_ERROR;
	if (in < 0) {
		dv     = -(picol_unumber_t)in;
		negate = 1;
	}
	do
		buf[i++] = picol_string_digits[dv % base];
	while ((dv /= base));
	if (negate)
		buf[i++] = '-';
	buf[i] = 0;
	picolReverse(buf, i);
	return PICKLE_OK;
}

static char *picolVsprintf(pickle_t *i, const char *fmt, va_list ap) {
	picolCheck(i);
	picolCheck(fmt);
	picol_stack_or_heap_t h = { .p = NULL, };
	PICOL_BUILD_BUG_ON(sizeof (h.buf) != PICOL_SMALL_RESULT_BUF_SZ);
	int needed = PICOL_SMALL_RESULT_BUF_SZ;
	if (picolStackOrHeapAlloc(i, &h, needed) != PICKLE_OK)
		return NULL;
	for (;;) {
		va_list mine;
		va_copy(mine, ap);
		const int r = vsnprintf(h.p, h.length, fmt, mine);
		va_end(mine);
		if (r < 0)
			goto fail;
		if (r < (int)h.length) /* Casting to 'int' is not ideal, but we have no choice */
			break;
		picolCheck(r < INT_MAX);
		if (picolStackOrHeapAlloc(i, &h, r + 1) != PICKLE_OK)
			goto fail;
	}
	if (!picolOnHeap(i, &h)) {
		char *r = picolStrdup(i, h.buf);
		if (picolStackOrHeapFree(i, &h) != PICKLE_OK) {
			(void)picolFree(i, r);
			return NULL;
		}
		return r;
	}
	return h.p;
fail:
	(void)picolStackOrHeapFree(i, &h);
	return NULL;
}

static int picolSetResultNumber(pickle_t *i, const picol_number_t result) {
	picolCheck(i);
	char buffy/*<3*/[PICOL_PRINT_NUMBER_BUF_SZ] = { 0, };
	if (picolNumberToString(buffy, result, 10) != PICKLE_OK)
		return error(i, "Error number");
	return picolSetResultString(i, buffy);
}

static int picolSetVarInteger(pickle_t *i, const char *name, const picol_number_t r) {
	picolCheck(i);
	picolCheck(name);
	char buffy[PICOL_PRINT_NUMBER_BUF_SZ] = { 0, };
	if (picolNumberToString(buffy, r, 10) != PICKLE_OK)
		return error(i, "Error number");
	return pickle_var_set(i, name, buffy);
}

static inline void picolAssertCommandPreConditions(pickle_t *i, const int argc, char **argv, void *pd) {
	/* PICOL_UNUSED is used to suppress warnings if NDEBUG is defined */
	PICOL_UNUSED(i);    picolCheck(i);
	PICOL_UNUSED(argc); picolCheck(argc >= 1);
	PICOL_UNUSED(argv); picolCheck(argv);
	PICOL_UNUSED(pd);   /* pd may be NULL*/
	if (PICOL_DEBUGGING)
		for (int j = 0; j < argc; j++)
			picolCheck(argv[j]);
}

static inline void picolAssertCommandPostConditions(pickle_t *i, const int retcode) {
	PICOL_UNUSED(i);  picolCheck(i);
	picolCheck(i->initialized);
	picolCheck(i->result);
	picolCheck(i->level >= 0);
	PICOL_UNUSED(retcode); /* arbitrary returns codes allowed, otherwise picolCheck((retcode >= 0) && (retcode < PICKLE_LAST_ENUM)); */
}

static int picolFreeArgList(pickle_t *i, const int argc, char **argv) {
	picolCheck(i);
	picolImplies(argc > 0, argv);
	int r = 0;
	for (int j = 0; j < argc; j++)
		if (picolFree(i, argv[j]) != PICKLE_OK)
			r = -1;
	if (picolFree(i, argv) != PICKLE_OK)
		r = -1;
	return r;
}

static int picolHexCharToNibble(int c) {
	c = tolower(c);
	if ('a' <= c && c <= 'f')
		return 0xa + c - 'a';
	return c - '0';
}

/* converts up to two characters and returns number of characters converted */
static int picolHexStr2ToInt(const char *str, int *const val) {
	picolCheck(str);
	picolCheck(val);
	*val = 0;
	if (!isxdigit(*str))
		return 0;
	*val = picolHexCharToNibble(*str++);
	if (!isxdigit(*str))
		return 1;
	*val = (*val << 4) + picolHexCharToNibble(*str);
	return 2;
}

static int picolUnEscape(char *r, size_t length) {
	picolCheck(r);
	if (!length)
		return -1;
	size_t k = 0;
	for (size_t j = 0, ch = 0; (ch = r[j]) && k < length; j++, k++) {
		if (ch == '\\') {
			j++;
			switch (r[j]) {
			case '\0': return -1;
			case '\n': k--;         break; /* multi-line hack (Unix line-endings only) */
			case '\\': r[k] = '\\'; break;
			case  'a': r[k] = '\a'; break;
			case  'b': r[k] = '\b'; break;
			case  'e': r[k] = 27;   break;
			case  'f': r[k] = '\f'; break;
			case  'n': r[k] = '\n'; break;
			case  'r': r[k] = '\r'; break;
			case  't': r[k] = '\t'; break;
			case  'v': r[k] = '\v'; break;
			case  'x': {
				int val = 0;
				const int pos = picolHexStr2ToInt(&r[j + 1], &val);
				if (pos < 1)
					return -2;
				j += pos;
				r[k] = val;
				break;
			}
			default:
				r[k] = r[j]; break;
			}
		} else {
			r[k] = ch;
		}
	}
	r[k] = '\0';
	return k;
}

static char *picolEscape(pickle_t *i, const char *s, size_t length) {
	picolCheck(i);
	picolCheck(s);
	char *m = picolMalloc(i, length + 2 + 1/*NUL*/);
	if (!m)
		return m;
	m[0] = '{';
	if (length)
		picolMove(m + 1, s, length);
	m[length + 1] = '}';
	m[length + 2] = '\0';
	return m;
}

static int picolStringNeedsEscaping(const char *s) {
	picolCheck(s);
	long braces = 0;
	char start = s[0], end = 0, ch = 0, sp = 0;
	for (size_t i = 0; (ch = s[i]); s++) {
		end = ch;
		if (picolLocateChar(picol_string_whitespace, ch))
			sp = 1;
		if (picolLocateChar("[]$", ch))
			sp = 1;
		if (ch == '{') braces++;
		if (ch == '}') braces--;
		if (ch == '\\') {
			ch = s[++i];
			sp = 1;
			if (!ch)
				return 1;
		}
	}
	if (!start || sp)
		return braces || !(start == '{' && end == '}');
	return 0;
}

static const char *picolTrimLeft(const char *search_class, const char *s) { /* Returns pointer to s */
	picolCheck(search_class);
	picolCheck(s);
	size_t j = 0, k = 0;
	while (s[j] && picolLocateChar(search_class, s[j++]))
		k = j;
	return &s[k];
}

static const char *picolLastUnmatching(const char *search_class, const char *s) {
	picolCheck(search_class);
	picolCheck(s);
	const size_t length = picolStrlen(s);
	size_t j = length - 1;
	if (j > length)
		return &s[0];
	while (j > 0 && picolLocateChar(search_class, s[j]))
		j--;
	return &s[j];
}

static char *picolTrimRight(const char *search_class, char *s) { /* Modifies argument */
	picolCheck(search_class);
	picolCheck(s);
	const size_t j = picolLastUnmatching(search_class, s) - s;
	if (s[j])
		s[j + !picolLocateChar(search_class, s[j])] = '\0';
	return s;
}

static char *picolTrim(const char *search_class, char *s) { /* Modifies argument */
	picolCheck(search_class);
	picolCheck(s);
	return picolTrimRight(search_class, (char*)picolTrimLeft(search_class, s));
}

static char *picolConcatenate(pickle_t *i, const char *join, const int argc, char **argv, const int doEscape, const int except, const int trim) {
	picolCheck(i);
	picolCheck(join);
	picolCheck(argc >= 0);
	picolImplies(argc > 0, argv != NULL);
	picol_stack_or_heap_t h = { .p = NULL, };
	if (argc == 0)
		return picolStrdup(i, "");
	const size_t jl = picolStrlen(join);
	size_t l = 0, lo = 0;
	char   *esc = picolMalloc(i, argc * sizeof *esc); /* NB. we could allocate this in pickle_t and grow as needed */
	size_t  *ls = picolMalloc(i, argc * sizeof *ls);
	char *str = NULL;
	int args = 0;
	if (!esc || !ls)
		goto end;
	picolZero(esc, argc * sizeof *esc);
	for (int j = 0; j < argc; j++) {
		if (!argv[j])
			continue;
		args++;
		const size_t sz = picolStrlen(argv[j]);
		if (doEscape && j != except)
			esc[j] = picolStringNeedsEscaping(argv[j]);
		ls[j] = sz;
		l += sz + jl + (2 * esc[j]);
	}
	if (PICOL_USE_MAX_STRING && ((l + 1) >= PICKLE_MAX_STRING))
		goto end;
	if (picolStackOrHeapAlloc(i, &h, l + 1) != PICKLE_OK)
		goto end;
	lo = l;
	l = 0;
	for (int j = 0, k = 0; j < argc; j++) {
		char *arg = argv[j];
		if (!arg)
			continue;

		k++;
		if (trim) {
			char *lt = (char*)picolTrimLeft(picol_string_whitespace, arg);
			char *rt = (char*)picolLastUnmatching(picol_string_whitespace, lt);
			if (*lt == '\0')
				continue;
			ls[j] = 1 + (rt - lt);
			arg = lt;
		}

		const int escape = esc[j];
		char *f = escape ? picolEscape(i, arg, ls[j]) : NULL;
		char *p = escape ? f : arg;
		if (!p)
			goto end;
		picolImplies(PICOL_USE_MAX_STRING, l < PICKLE_MAX_STRING);
		picolCheck(escape == 0 || escape == 1);
		picolCheck((l + ls[j] + (2 * escape)) <= lo);
		(void)lo;
		picolMove(h.p + l, p, ls[j] + (2 * escape));
		l += ls[j] + (2 * escape);
		if (jl && k < args) {
			picolImplies(PICOL_USE_MAX_STRING, l < PICKLE_MAX_STRING);
			picolMove(h.p + l, join, jl);
			l += jl;
		}
		if (picolFree(i, f) != PICKLE_OK)
			goto end;
	}
	h.p[l] = '\0';
	str = picolOnHeap(i, &h) ? h.p : picolStrdup(i, h.p);
end:
	(void)picolFree(i, ls);
	(void)picolFree(i, esc);
	/* NB. Do not call 'picolStackOrHeapFree(i, &h)' */
	return str;
}

static picol_args_t picolArgs(pickle_t *i, pickle_parser_opts_t *o, const char *s) {
	picolCheck(i);
	picolCheck(s);
	picolCheck(o);
	picol_args_t r = { .argc = 0, .argv = NULL, };
	picol_parser_t p = { .p = NULL, };
	picolParserInitialize(&p, o, s);
	for (;;) {
		if (picolGetToken(&p) != PICKLE_OK)
			goto err;
		if (p.type == PCL_EOF)
			break;
		if (p.type == PCL_STR || p.type == PCL_VAR || p.type == PCL_CMD || p.type == PCL_ESC) {
			const size_t tl = (p.end - p.start) + 1;
			char *t = picolMalloc(i, tl + 1);
			char **old = r.argv;
			if (!t)
				goto err;
			picolMove(t, p.start, tl);
			t[tl] = '\0';
			if (!(r.argv = picolRealloc(i, r.argv, sizeof(char*)*(r.argc + 1)))) {
				r.argv = old;
				goto err;
			}
			r.argv[r.argc++] = t;
		}
	}
	picolImplies(r.argc, r.argv);
	return r;
err:
	(void)picolFreeArgList(i, r.argc, r.argv);
	return (picol_args_t){ -1, NULL, };
}

static char **picolArgsGrow(pickle_t *i, int *argc, char **argv) {
	picolCheck(i);
	picolCheck(argc);
	const int n = *argc + 1;
	picolCheck(n >= 0);
	char **old = argv;
	if (!(argv = picolRealloc(i, argv, sizeof(char*) * n))) {
		(void)picolFreeArgList(i, *argc, old);
		*argc = 0;
		return NULL;
	}
	*argc = n;
	argv[n - 1] = NULL;
	return argv;
}

static char **picolArgsCopy(pickle_t *i, int argc, char **argv, unsigned grow) {
	picolCheck(i);
	picolCheck(argv);
	char **as = picolMalloc(i, (grow + argc) * sizeof (char*));
	if (!as)
		return NULL;
	picolZero(&as[argc], grow * sizeof (char*));
	picolMove(as, argv, argc * sizeof (char*));
	return as;
}

static inline int picolDoSpecialCommand(pickle_t *i, pickle_command_t *cmd, const char *name, int argc, char **argv) {
	picolCheck(i);
	picolCheck(argv);
	picolCheck(cmd);
	char **as = picolArgsCopy(i, argc, argv, 1);
	if (!as)
		return PICKLE_ERROR;
	picolMove(&as[1], &as[0], argc * sizeof (char*));
	as[0] = (char*)name;
	picolAssertCommandPreConditions(i, argc + 1, as, cmd->privdata);
	const int r = cmd->func(i, argc + 1, as, cmd->privdata);
	picolAssertCommandPostConditions(i, r);
	if (picolFree(i, as) != PICKLE_OK)
		return PICKLE_ERROR;
	return r;
}

static inline int picolDoCommand(pickle_t *i, int argc, char *argv[]) {
	picolCheck(i);
	picolCheck(argc >= 1);
	picolCheck(argv);
	i->cmdcount++;
	if (i->trace && !(i->inside_trace)) {
		pickle_command_t *t = picolGetCommand(i, "tracer");
		if (t) {
			i->inside_trace = 1;
			const int r = picolDoSpecialCommand(i, t, "tracer", argc, argv);
			i->inside_trace = 0;
			if (r != PICKLE_OK) {
				i->trace = 0;
				return r;
			}
		}
	}
	if (picolSetResultEmpty(i) != PICKLE_OK)
		return PICKLE_ERROR;
	pickle_command_t *c = picolGetCommand(i, argv[0]);
	if (c == NULL) {
		if (i->inside_unknown || ((c = picolGetCommand(i, "unknown")) == NULL))
			return error(i, "Error unknown command %s", argv[0]);
		i->inside_unknown = 1;
		const int r = picolDoSpecialCommand(i, c, "unknown", argc, argv);
		i->inside_unknown = 0;
		return r;
	}
	picolAssertCommandPreConditions(i, argc, argv, c->privdata);
	const int r = c->func(i, argc, argv, c->privdata);
	picolAssertCommandPostConditions(i, r);
	return r;
}

static int picolEvalAndSubst(pickle_t *i, pickle_parser_opts_t *o, const char *eval) {
	picolCheck(i);
	picolCheck(i->initialized);
	/* NB: picolCheck(o || !o); */
	picolCheck(eval);
	picol_parser_t p = { .p = NULL, };
	int retcode = PICKLE_OK, argc = 0;
	char **argv = NULL;
	if (picolSetResultEmpty(i) != PICKLE_OK)
		return PICKLE_ERROR;
	if (i->evals++ >= PICKLE_MAX_RECURSION) {
		i->evals--;
		return error(i, "Error recursion %d", PICKLE_MAX_RECURSION);
	}
	picolParserInitialize(&p, o, eval);
	for (int prevtype = p.type;;) {
		if (picolGetToken(&p) != PICKLE_OK) {
			retcode = error(i, "Error parse %s", eval);
			goto err;
		}
		if (p.type == PCL_EOF)
			break;
		int tlen = p.end - p.start + 1;
		if (tlen < 0)
			tlen = 0;
		char *t = picolMalloc(i, tlen + 1); /* Using 'picolStackOrHeapAlloc' may complicate things. */
		if (!t) {
			retcode = PICKLE_ERROR;
			goto err;
		}
		picolMove(t, p.start, tlen);
		t[tlen] = '\0';
		if (p.type == PCL_VAR) {
			pickle_var_t * const v = picolGetVar(i, t, 1);
			if (!v) {
				retcode = error(i, "Error variable %s", t);
				(void)picolFree(i, t);
				goto err;
			}
			if (picolFree(i, t) != PICKLE_OK)
				goto err;
			if (!(t = picolStrdup(i, picolGetVarVal(v)))) {
				retcode = PICKLE_ERROR;
				goto err;
			}
		} else if (p.type == PCL_CMD) {
			retcode = picolEvalAndSubst(i, NULL, t); // NB!
			if (picolFree(i, t) != PICKLE_OK)
				goto err;
			if (retcode != PICKLE_OK)
				goto err;
			if (!(t = picolStrdup(i, i->result))) {
				retcode = PICKLE_ERROR;
				goto err;
			}
		} else if (p.type == PCL_ESC) {
			if (picolUnEscape(t, tlen + 1/*NUL terminator*/) < 0) {
				retcode = error(i, "Error parse %s", t); /* BUG: %s is probably mangled by now */
				(void)picolFree(i, t);
				goto err;
			}
		} else if (p.type == PCL_SEP) {
			prevtype = p.type;
			if (picolFree(i, t) != PICKLE_OK)
				goto err;
			continue;
		}

		if (p.type == PCL_EOL) { /* We have a complete command + args. Call it! */
			if (picolFree(i, t) != PICKLE_OK)
				goto err;
			t = NULL;
			prevtype = p.type;
			if (p.o.noeval) {
				char *result = picolConcatenate(i, " ", argc, argv, 0, -1, 0);
				if (!result) {
					retcode = PICKLE_ERROR;
					goto err;
				}
				if ((retcode = picolForceResult(i, result, 0)) != PICKLE_OK)
					goto err;
			} else {
				if (argc) {
					if ((retcode = picolDoCommand(i, argc, argv)) != PICKLE_OK)
						goto err;
				}
			}
			/* Prepare for the next command */
			if (picolFreeArgList(i, argc, argv) != PICKLE_OK)
				return PICKLE_ERROR;
			argv = NULL;
			argc = 0;
			continue;
		}

		if (prevtype == PCL_SEP || prevtype == PCL_EOL) { /* New token, append to the previous or as new arg? */
			char **old = argv;
			if (!(argv = picolRealloc(i, argv, sizeof(char*)*(argc + 1)))) {
				argv = old;
				retcode = PICKLE_ERROR;
				goto err;
			}
			argv[argc] = t;
			t = NULL;
			argc++;
		} else { /* Interpolation */
			picolCheck(argv);
			const int oldlen = picolStrlen(argv[argc - 1]), ilen = picolStrlen(t);
			char *arg = picolRealloc(i, argv[argc - 1], oldlen + ilen + 1);
			if (!arg) {
				retcode = PICKLE_ERROR;
				(void)picolFree(i, t);
				goto err;
			}
			argv[argc - 1] = arg;
			picolMove(argv[argc - 1] + oldlen, t, ilen);
			argv[argc - 1][oldlen + ilen] = '\0';
		}
		if (picolFree(i, t) != PICKLE_OK)
			goto err;
		prevtype = p.type;
	}
err:
	i->evals--;
	if (picolFreeArgList(i, argc, argv) != PICKLE_OK)
		return PICKLE_ERROR;
	return retcode;
}

static int picolEval(pickle_t *i, const char *t) {
	picolCheck(i);
	picolCheck(t);
	return picolEvalAndSubst(i, NULL, t);
}

/*Based on: <http://c-faq.com/lib/regex.html>, also see:
 <https://www.cs.princeton.edu/courses/archive/spr09/cos333/beautiful.html> */
static inline int picolMatch(const char *pat, const char *str, const int nocase, const size_t depth) {
	picolCheck(pat);
	picolCheck(str);
	assert(depth <= PICKLE_MAX_RECURSION);
	if (!depth) return -1; /* error: depth exceeded */
 again:
	switch (*pat) {
	case '\0': return !*str;
	case '*': { /* match any number of characters: normally '.*' */
		const int r = picolMatch(pat + 1, str, nocase, depth - 1);
		if (r)         return r;
		if (!*(str++)) return 0;
		goto again;
	}
	case '?':  /* match any single characters: normally '.' */
		if (!*str) return 0;
		pat++, str++;
		goto again;
	case '\\': /* escape character */
		if (!*(++pat)) return -2; /* error: missing escaped character */
		if (!*str)     return 0;
		/* fall through */
	default:
		if (nocase) {
			if (tolower(*pat) != tolower(*str)) return 0;
		} else {
			if (*pat != *str) return 0;
		}
		pat++, str++;
		goto again;
	}
	return -3; /* not reached */
}

static inline int picolIsFalse(const char *s) {
	picolCheck(s);
	static const char *negatory[] = { "0", "false", "off", "no", };
	for (size_t i = 0; i < (sizeof(negatory) / sizeof(negatory[0])); i++)
		if (!picolCompareCaseInsensitive(negatory[i], s))
			return 1;
	return 0;
}

static inline int picolIsTrue(const char *s) {
	picolCheck(s);
	static const char *affirmative[] = { "1", "true", "on", "yes", };
	for (size_t i = 0; i < (sizeof(affirmative) / sizeof(affirmative[0])); i++)
		if (!picolCompareCaseInsensitive(affirmative[i], s))
			return 1;
	return 0;
}

#define PICOL_TRCC (256)

typedef struct { short set[PICOL_TRCC]; /* x < 0 == delete, x | 0x100 == squeeze, x < 0x100 == translate */ } picol_tr_t;

static inline int picolTrInit(picol_tr_t *t, int translate, int compliment, const char *set1, const char *set2) {
	picolCheck(t);
	picolCheck(set1);
	picolCheck(set2);
	char schoen[PICOL_TRCC+1] = { 0, };

	if (compliment) {
		char haesslich[PICOL_TRCC] = { 0, };
		for (unsigned char ch = 0; (ch = *set1); set1++)
			haesslich[ch] = 1;
		for (size_t i = 0, j = 0; i < sizeof haesslich; i++)
			if (haesslich[i] == 0)
				schoen[j++] = i;
		set1 = &schoen[1]; /* cannot deal with NUL */
	}

	for (size_t i = 0; i < PICOL_TRCC; i++)
		t->set[i] = i;

	for (unsigned char from = 0; (from = *set1); set1++)
		if (translate) {
			const unsigned char to = *set2;
			t->set[to]   |= 0x100;
			t->set[from] = (t->set[from] & 0x100) | to;
			if (to && set2[1])
				set2++;
		} else { /* delete */
			t->set[from] = -1;
		}

	return 0;
}

static inline int picolTr(const picol_tr_t *t, const int squeeze, const char *in, const size_t inlen, char *out, size_t outlen) {
	picolCheck(t);
	picolCheck(in);
	picolCheck(out);
	const short *s = t->set;
	short prev = -1;
	for (size_t i = 0, j = 0; i < inlen; i++) {
		const unsigned char inb = in[i];
		const short op = s[inb];
		if (op >= 0) {
			if (squeeze && op != inb && op == prev)
				continue;
			if (j >= outlen)
				return -1;
			out[j++] = op & 0xFF;
			prev = op;
		} /* else delete */
	}
	return 0;
}

static inline int picolCommandTranslate(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc != 4 && argc != 5, "[csdr] string string?: translate characters");
	int compliment = 0, translate = 1, squeeze = 0;
	const char *op = argv[1], *set1 = argv[2];

	for (unsigned char ch = 0; (ch = *op); op++) {
		switch (ch) {
		case 'c': compliment = 1; break;
		case 's': squeeze    = 1; break;
		case 'd': translate  = 0; break;
		case 'r': translate  = 1; break;
		default:
			return error(i, "Error %s %s", op, argv[0]);
		}
	}

	const char *set2 = argc == 4 ? set1 : argv[3];
	const char *input = argv[3 + (argc == 5)];
	picol_tr_t t = { .set = { 0, } };

	if (picolTrInit(&t, translate, compliment, set1, set2) < 0)
		return error(i, "Error %s %s", op, argv[0]);
	const size_t ml = picolStrlen(input) + 1;
	char *m = picolMalloc(i, ml);
	if (!m)
		return PICKLE_ERROR;
	const int r = picolTr(&t, squeeze, input, ml, m, ml) < 0 ?
		error(i, "Error %s %s", op, argv[0]) :
		picolSetResultString(i, m);
	if (picolFree(i, m) != PICKLE_OK)
		return PICKLE_ERROR;
	return r;
}

enum { TRIM, TRIM_RIGHT, TRIM_LEFT, };

static inline int picolTrimOps(pickle_t *i, picol_stack_or_heap_t *h, int op, const char *arg, const char *search_class) {
	picolCheck(i);
	picolCheck(h);
	picolCheck(arg);
	picolCheck(search_class);
	picolCheck(op == TRIM_LEFT || op == TRIM_RIGHT || op == TRIM);
	if (op == TRIM_LEFT)
		return picolSetResultString(i, picolTrimLeft(search_class, arg));
	const size_t l = picolStrlen(arg);
	if (picolStackOrHeapAlloc(i, h, l + 1) != PICKLE_OK)
		return PICKLE_ERROR;
	picolMove(h->p, arg, l + 1);
	const int r = picolSetResultString(i, op == TRIM ? picolTrim(search_class, h->p) : picolTrimRight(search_class, h->p));
	if (picolStackOrHeapFree(i, h) != PICKLE_OK)
		return PICKLE_ERROR;
	return r;
}

static inline int picolCommandString(pickle_t *i, const int argc, char **argv, void *pd) { /* Big! */
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc < 3, "subcommand opts...: perform string operations depending on subcommand");
	const char *rq = argv[1];
	picol_stack_or_heap_t h = { .p = NULL, };
	if (argc == 3) {
		const char *arg1 = argv[2];
		if (!picolCompare(rq, "trimleft"))
			return picolTrimOps(i, &h, TRIM_LEFT, arg1, picol_string_whitespace);
		if (!picolCompare(rq, "trimright"))
			return picolTrimOps(i, &h, TRIM_RIGHT, arg1, picol_string_whitespace);
		if (!picolCompare(rq, "trim"))
			return picolTrimOps(i, &h, TRIM, arg1, picol_string_whitespace);
		if (!picolCompare(rq, "length") /*|| !picolCompare(rq, "bytelength")*/)
			return picolSetResultNumber(i, picolStrlen(arg1));
		if (!picolCompare(rq, "toupper") || !picolCompare(rq, "tolower")) {
			const int lower = rq[2] == 'l';
			if (picolStackOrHeapAlloc(i, &h, picolStrlen(arg1) + 1) != PICKLE_OK)
				return PICKLE_ERROR;
			size_t j = 0;
			for (j = 0; arg1[j]; j++)
				h.p[j] = lower ? tolower(arg1[j]) : toupper(arg1[j]);
			h.p[j] = 0;
			if (picolOnHeap(i, &h))
				return picolForceResult(i, h.p, 0);
			const int r = picolSetResultString(i, h.p);
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!picolCompare(rq, "reverse")) {
			const size_t l = picolStrlen(arg1);
			if (picolStackOrHeapAlloc(i, &h, l + 1) != PICKLE_OK)
				return PICKLE_ERROR;
			picolMove(h.p, arg1, l + 1);
			const int r = picolSetResultString(i, picolReverse(h.p, l));
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!picolCompare(rq, "ordinal"))
			return picolSetResultNumber(i, arg1[0]);
		if (!picolCompare(rq, "char")) {
			picol_number_t v = 0;
			if (picolStringToNumber(i, arg1, &v) != PICKLE_OK)
				return PICKLE_ERROR;
			char b[] = { v, 0, };
			return picolSetResultString(i, b);
		}
		if (!picolCompare(rq, "dec2hex")) {
			picol_number_t hx = 0;
			if (picolStringToNumber(i, arg1, &hx) != PICKLE_OK)
				return PICKLE_ERROR;
			PICOL_BUILD_BUG_ON(PICOL_SMALL_RESULT_BUF_SZ < PICOL_PRINT_NUMBER_BUF_SZ);
			if (picolNumberToString(h.buf, hx, 16) != PICKLE_OK)
				return error(i, "Error %s %s", rq, h.buf);
			return picolSetResultString(i, h.buf);
		}
		if (!picolCompare(rq, "hex2dec")) {
			picol_number_t l = 0;
			if (picolConvertBaseNNumber(i, arg1, &l, 16) != PICKLE_OK)
				return error(i, "Error %s %s", rq, arg1);
			return picolSetResultNumber(i, l);
		}
		if (!picolCompare(rq, "hash"))
			return picolSetResultNumber(i, picolHashString(arg1));
	} else if (argc == 4) {
		const char *arg1 = argv[2], *arg2 = argv[3];
		if (!picolCompare(rq, "trimleft"))
			return picolTrimOps(i, &h, TRIM_LEFT, arg1, arg2);
		if (!picolCompare(rq, "trimright"))
			return picolTrimOps(i, &h, TRIM_RIGHT, arg1, arg2);
		if (!picolCompare(rq, "trim"))
			return picolTrimOps(i, &h, TRIM, arg1, arg2);
		if (!picolCompare(rq, "match"))  {
			const int r = picolMatch(arg1, arg2, 0, PICKLE_MAX_RECURSION - i->level);
			if (r < 0)
				return error(i, "Error recursion %d", r);
			return picolSetResultNumber(i, r);
		}
		if (!picolCompare(rq, "equal"))
			return picolSetResultNumber(i, !picolCompare(arg1, arg2));
		if (!picolCompare(rq, "unequal"))
			return picolSetResultNumber(i, !!picolCompare(arg1, arg2));
		if (!picolCompare(rq, "compare"))
			return picolSetResultNumber(i, picolCompare(arg1, arg2));
		if (!picolCompare(rq, "compare-no-case"))
			return picolSetResultNumber(i, picolCompareCaseInsensitive(arg1, arg2));
		if (!picolCompare(rq, "index"))   {
			picol_number_t index = 0;
			if (picolStringToNumber(i, arg2, &index) != PICKLE_OK)
				return PICKLE_ERROR;
			const picol_number_t length = picolStrlen(arg1);
			if (index < 0)
				index = length + index;
			if (index > length)
				index = length - 1;
			if (index < 0)
				index = 0;
			const char ch[2] = { arg1[index], '\0' };
			return picolSetResultString(i, ch);
		}
		if (!picolCompare(rq, "is")) { /* NB: These might be locale dependent. */
			if (!picolCompare(arg1, "alnum"))    { while (isalnum(*arg2))  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!picolCompare(arg1, "alpha"))    { while (isalpha(*arg2))  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!picolCompare(arg1, "digit"))    { while (isdigit(*arg2))  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!picolCompare(arg1, "graph"))    { while (isgraph(*arg2))  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!picolCompare(arg1, "lower"))    { while (islower(*arg2))  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!picolCompare(arg1, "print"))    { while (isprint(*arg2))  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!picolCompare(arg1, "punct"))    { while (ispunct(*arg2))  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!picolCompare(arg1, "space"))    { while (isspace(*arg2))  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!picolCompare(arg1, "upper"))    { while (isupper(*arg2))  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!picolCompare(arg1, "xdigit"))   { while (isxdigit(*arg2)) arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!picolCompare(arg1, "ascii"))    { while (*arg2 && !(0x80 & *arg2)) arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!picolCompare(arg1, "control"))  { while (*arg2 && iscntrl(*arg2))  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!picolCompare(arg1, "wordchar")) { while (isalnum(*arg2) || *arg2 == '_')  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!picolCompare(arg1, "false"))    { return picolSetResultNumber(i, picolIsFalse(arg2)); }
			if (!picolCompare(arg1, "true"))     { return picolSetResultNumber(i, picolIsTrue(arg2)); }
			if (!picolCompare(arg1, "boolean"))  { return picolSetResultNumber(i, picolIsTrue(arg2) || picolIsFalse(arg2)); }
			if (!picolCompare(arg1, "integer"))  { return picolSetResultNumber(i, picolStringToNumber(i, arg2, &(picol_number_t){0l}) == PICKLE_OK); }
			/* Missing: double */
		}
		if (!picolCompare(rq, "repeat")) {
			picol_number_t count = 0, j = 0;
			const size_t length = picolStrlen(arg1);
			if (picolStringToNumber(i, arg2, &count) != PICKLE_OK)
				return PICKLE_ERROR;
			if (count < 0)
				return error(i, "Error %s %s", rq, arg2);
			if (picolStackOrHeapAlloc(i, &h, (count * length) + 1) != PICKLE_OK)
				return PICKLE_ERROR;
			picolImplies(PICOL_USE_MAX_STRING, ((((count - 1) * length) + length) < PICKLE_MAX_STRING));
			for (; j < count; j++)
				picolMove(&h.p[j * length], arg1, length);
			h.p[j * length] = 0;
			const int r = picolSetResultString(i, h.p);
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!picolCompare(rq, "first")) {
			const char *found = picolFind(arg2, arg1);
			if (!found)
				return picolSetResultNumber(i, -1);
			return picolSetResultNumber(i, found - arg2);
		}
		if (!picolCompare(rq, "last")) {
			const char *found = picolRFind(arg2, arg1);
			if (!found)
				return picolSetResultNumber(i, -1);
			return picolSetResultNumber(i, found - arg2);
		}
		if (!picolCompare(rq, "base2dec")) {
			picol_number_t b = 0, n = 0;
			if (picolStringToNumber(i, arg2, &b) != PICKLE_OK)
				return PICKLE_ERROR;
			if (!picolIsBaseValid(b))
				return error(i, "Error %s %s", rq, arg2);
			if (picolConvertBaseNNumber(i, arg1, &n, b) != PICKLE_OK)
				return error(i, "Error %s %s", rq, arg1);
			return picolSetResultNumber(i, n);
		}
		if (!picolCompare(rq, "dec2base")) {
			picol_number_t b = 0, n = 0;
			if (picolStringToNumber(i, arg2, &b) != PICKLE_OK)
				return PICKLE_ERROR;
			if (!picolIsBaseValid(b))
				return error(i, "Error %s %s", rq, arg2);
			if (picolStringToNumber(i, arg1, &n) != PICKLE_OK)
				return error(i, "Error %s %s", rq, arg1);
			PICOL_BUILD_BUG_ON(PICOL_SMALL_RESULT_BUF_SZ < PICOL_PRINT_NUMBER_BUF_SZ);
			if (picolNumberToString(h.buf, n, b) != PICKLE_OK)
				return error(i, "Error %s %s", rq, arg1);
			return picolSetResultString(i, h.buf);
		}
	} else if (argc == 5) {
		const char *arg1 = argv[2], *arg2 = argv[3], *arg3 = argv[4];
		if (!picolCompare(rq, "first"))      {
			const picol_number_t length = picolStrlen(arg2);
			picol_number_t start  = 0;
			if (picolStringToNumber(i, arg3, &start) != PICKLE_OK)
				return PICKLE_ERROR;
			if (start < 0 || start >= length)
				return picolSetResultEmpty(i);
			const char *found = picolFind(arg2 + start, arg1);
			if (!found)
				return picolSetResultNumber(i, -1);
			return picolSetResultNumber(i, found - arg2);
		}
		if (!picolCompare(rq, "range")) {
			const picol_number_t length = picolStrlen(arg1);
			picol_number_t first = 0, last = 0;
			if (picolStringToNumber(i, arg2, &first) != PICKLE_OK)
				return PICKLE_ERROR;
			if (picolStringToNumber(i, arg3, &last) != PICKLE_OK)
				return PICKLE_ERROR;
			if (first > last)
				return picolSetResultEmpty(i);
			first = MIN(length, MAX(0, first));
			last  = MIN(length, MAX(0, last));
			const picol_number_t diff = (last - first) + 1;
			if (diff <= 1)
				return picolSetResultEmpty(i);
			if (picolStackOrHeapAlloc(i, &h, diff + 1) != PICKLE_OK)
				return PICKLE_ERROR;
			picolMove(h.p, &arg1[first], diff);
			h.p[diff] = 0;
			const int r = picolSetResultString(i, h.p);
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!picolCompare(rq, "match"))  {
			if (picolCompare(arg1, "-nocase"))
				return error(i, "Error option %s", arg1);
			const int r = picolMatch(arg2, arg3, 1, PICKLE_MAX_RECURSION - i->level);
			if (r < 0)
				return error(i, "Error recursion %d", r);
			return picolSetResultNumber(i, r);
		}
		if (!picolCompare(rq, "tr"))
			return picolCommandTranslate(i, argc - 1, argv + 1, NULL);
	} else if (argc == 6) {
		const char *arg1 = argv[2], *arg2 = argv[3], *arg3 = argv[4], *arg4 = argv[5];
		if (!picolCompare(rq, "replace")) {
			const picol_number_t extend = picolStrlen(arg4);
			const picol_number_t length = picolStrlen(arg1);
			picol_number_t first = 0, last = 0;
			if (picolStringToNumber(i, arg2, &first) != PICKLE_OK)
				return PICKLE_ERROR;
			if (picolStringToNumber(i, arg3, &last) != PICKLE_OK)
				return PICKLE_ERROR;
			if (first < 0)
				first = 0;
			if (last > length)
				last = length;
			if (first > last || first > length || last < 0)
				return picolSetResultString(i, arg1);
			const picol_number_t diff = (last - first);
			const picol_number_t resulting = (length - diff) + extend + 1;
			picolCheck(diff >= 0 && length >= 0);
			if (picolStackOrHeapAlloc(i, &h, resulting) != PICKLE_OK)
				return PICKLE_ERROR;
			picolMove(h.p,                  arg1,            first);
			picolMove(h.p + first,          arg4,            extend);
			picolMove(h.p + first + extend, arg1 + last + 1, length - last);
			const size_t index = first + extend + length - last;
			picolCheck(index < h.length);
			h.p[index] = '\0';
			if (picolOnHeap(i, &h))
				return picolForceResult(i, h.p, 0);
			const int r = picolSetResultString(i, h.p);
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!picolCompare(rq, "tr"))
			return picolCommandTranslate(i, argc - 1, argv + 1, NULL);
	} /* NB. We could call 'unknown' here, this would allow us to add commands later. */
	return error(i, "Error option %s", argv[0]);
}

static inline int picolCommandEqual(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc != 3, "string string: string equality");
	return picolForceResult(i, picolCompare(argv[1], argv[2]) ? "0" : "1", 1);
}

static inline int picolCommandNotEqual(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc != 3, "string string: string inequality");
	return picolForceResult(i, picolCompare(argv[1], argv[2]) ? "1" : "0", 1);
}

static int picolCommandIncr(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	picol_number_t incr = 1, n = 0;
	PICOL_ARITY(argc != 2 && argc != 3, "number: increment a number");
	if (argc == 3)
		if (picolStringToNumber(i, argv[2], &incr) != PICKLE_OK)
			return PICKLE_ERROR;
	pickle_var_t *v = picolGetVar(i, argv[1], 1);
	if (!v)
		return error(i, "Error %s %s", argv[0], argv[1]);
	const char *ns = picolGetVarVal(v);
	if (picolStringToNumber(i, ns, &n) != PICKLE_OK)
		return PICKLE_ERROR;
	n += incr;
	if (picolSetVarInteger(i, argv[1], n) != PICKLE_OK)
		return PICKLE_ERROR;
	return picolSetResultNumber(i, n);
}

enum { UNOT, UINV, UABS, UBOOL, UNEGATE, };
enum {
	BADD,  BSUB,    BMUL,    BDIV, BMOD,
	BMORE, BMEQ,    BLESS,   BLEQ, BEQ,
	BNEQ,  BLSHIFT, BRSHIFT, BAND, BOR,
	BXOR,  BMIN,    BMAX,    BPOW, BLOG,
	BLAND, BLOR,
};

static inline int picolCommandMathUnary(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_ARITY(argc != 2, "number: unary operator");
	picol_number_t a = 0;
	if (picolStringToNumber(i, argv[1], &a) != PICKLE_OK)
		return PICKLE_ERROR;
	switch ((intptr_t)(char*)pd) {
	case UNOT:    a = !a; break;
	case UINV:    a = ~a; break;
	case UABS:    a = a < 0 ? -a : a; /* if (a == PICOL_NUMBER_MIN) return error(i, "Error %s %s", argv[0], argv[1]); */ break;
	case UBOOL:   a = !!a; break;
	case UNEGATE: a = -a; break;
	default: return error(i, "Error %s %s", argv[0], argv[1]);
	}
	return picolSetResultNumber(i, a);
}

static inline int picolCommandMath(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	PICOL_ARITY(argc < 3, "number number...: standard mathematical operator");
	picol_number_t a = 0, b = 0;
	if (picolStringToNumber(i, argv[1], &a) != PICKLE_OK)
		return PICKLE_ERROR;
	picol_number_t c = 1;
	const unsigned op = (intptr_t)(char*)pd;
	for (int j = 2; j < argc; j++) {
		if (picolStringToNumber(i, argv[j], &b) != PICKLE_OK)
			return PICKLE_ERROR;
		switch (op) {
		case BADD:    c = a + b; a = c; break;
		case BSUB:    c = a - b; a = c; break;
		case BMUL:    c = a * b; a = c; break;
		case BDIV:    if (b) { c = a / b; a = c; } else { return error(i, "Error %s %s", argv[0], argv[j]); } break;
		case BMOD:    if (b) { c = a % b; a = c; } else { return error(i, "Error %s %s", argv[0], argv[j]); } break;
		case BMORE:   c &= a > b; break;
		case BMEQ:    c &= a >= b; break;
		case BLESS:   c &= a < b; break;
		case BLEQ:    c &= a <= b; break;
		case BEQ:     c &= a == b; break;
		case BNEQ:    c &= a != b; break;
		case BLSHIFT: c = ((picol_unumber_t)a) << b; a = c; break;
		case BRSHIFT: c = ((picol_unumber_t)a) >> b; a = c; break;
		case BAND:    c = a & b; a = c; break;
		case BOR:     c = a | b; a = c; break;
		case BLAND:   c = a && b; a = c; break;
		case BLOR:    c = a || b; a = c; break;
		case BXOR:    c = a ^ b; a = c; break;
		case BMIN:    c = MIN(a, b); a = c; break;
		case BMAX:    c = MAX(a, b); a = c; break;
		case BPOW:    if (picolPower(a, b, &c)     != PICKLE_OK) return error(i, "Error operation %s", argv[0]); a = c; break;
		case BLOG:    if (picolLogarithm(a, b, &c) != PICKLE_OK) return error(i, "Error operation %s", argv[0]); a = c; break;
		default: return error(i, "Error operation %s", argv[0]);
		}
	}
	return picolSetResultNumber(i, c);
}

static int picolCommandSet(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	PICOL_ARITY(argc != 3 && argc != 2, "name string?: set a variable and return it");
	if (argc == 2) {
		const char *r = NULL;
		const int retcode = pickle_var_get(i, argv[1], &r);
		if (retcode != PICKLE_OK || !r)
			return error(i, "Error variable %s", argv[1]);
		return picolSetResultString(i, r);
	}
	if (pickle_var_set(i, argv[1], argv[2]) != PICKLE_OK)
		return PICKLE_ERROR;
	return picolSetResultString(i, argv[2]);
}

static int picolCommandCatch(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	PICOL_ARITY(argc != 2 && argc != 3, "expression variable: evaluate expression and catch return code");
	const int r = picolEval(i, argv[1]);
	const char *s = NULL;
	if (pickle_result_get(i, &s) != PICKLE_OK)
		return PICKLE_ERROR;
	if (argc == 3)
		if (pickle_var_set(i, argv[2], s) != PICKLE_OK)
			return PICKLE_ERROR;
	return picolSetResultNumber(i, r);
}

static int picolCommandIf(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	PICOL_ARITY(argc < 3, "if {expr} {clause} [elseif {expr} {clause}]* [else {clause}]? : conditionally evaluate expressions");
	int j = 0;
	for (j = 3; j < argc;) { /* syntax check */
		if (!picolCompare("elseif", argv[j])) {
			j += 3;
			if ((argc - j) < 0)
				return error(i, "Error %s %s", argv[0], argv[j]);
		} else if (!picolCompare("else", argv[j])) {
			if ((argc - j) != 2)
				return error(i, "Error %s %s", argv[0], argv[j]);
			break;
		} else {
			return error(i, "Error %s %s", argv[0], argv[j]);
		}
	}
	for (j = 0; j < argc; j += 3) {
		if ((argc - j) == 2) { /* else */
			picolCheck(!picolCompare("else", argv[j]));
			picolCheck((j + 1) < argc);
			return picolEval(i, argv[j + 1]);
		}
		/* must be 'if' or 'elseif' */
		picolCheck((j + 2) < argc);
		const int r = picolEval(i, argv[j + 1]);
		if (r != PICKLE_OK)
			return r;
		if (!picolIsFalse(i->result))
			return picolEval(i, argv[j + 2]);
	}
	return PICKLE_OK;
}

static int picolCommandWhile(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc != 3, "condition clause: evaluate condition whilst clause is true");
	for (;;) {
		const int r1 = picolEval(i, argv[1]);
		if (r1 != PICKLE_OK)
			return r1;
		if (picolIsFalse(i->result))
			return PICKLE_OK;
		const int r2 = picolEval(i, argv[2]);
		switch (r2) {
		case PICKLE_OK:
		case PICKLE_CONTINUE:
			break;
		case PICKLE_BREAK:
			return PICKLE_OK;
		default:
			return r2;
		}
	}
}

static int picolCommandApply(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc < 2, "{{arg-list} {body}} args: evaluate 'body' with 'args'");
	const picol_args_t a = picolArgs(i, &(pickle_parser_opts_t){ 1, 1, 1, 1 }, argv[1]);
	if (a.argc < 0)
		return PICKLE_ERROR;
	if (a.argc != 2) {
		(void)picolFreeArgList(i, a.argc, a.argv);
		return error(i, "Error option %s", argv[1]);
	}
	const int r = picolCommandCallProc(i, argc - 1, argv + 1, a.argv);
	if (picolFreeArgList(i, a.argc, a.argv) != PICKLE_OK)
		return PICKLE_ERROR;
	return r;
}

static int picolCommandFor(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc != 5, "setup condition after clause: evaluate clause whilst condition is true");
	const int r1 = picolEval(i, argv[1]);
	if (r1 != PICKLE_OK)
		return r1;
	for (;;) {
		const int r2 = picolEval(i, argv[2]);
		if (r2 != PICKLE_OK)
			return r2;
		if (picolIsFalse(i->result))
			return PICKLE_OK;
		const int r3 = picolEval(i, argv[4]);
		switch (r3) {
		case PICKLE_OK:
		case PICKLE_CONTINUE:
			break;
		case PICKLE_BREAK:
			return PICKLE_OK;
		default:
			return r3;
		}
		const int r4 = picolEval(i, argv[3]);
		if (r4 != PICKLE_OK)
			return r4;
	}
}

static inline int picolCommandLRepeat(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc < 3, "number string...: repeat a string to form a list");
	picol_number_t count = 0;
	if (picolStringToNumber(i, argv[1], &count) != PICKLE_OK)
		return PICKLE_ERROR;
	if (count < 0)
		return error(i, "Error option %s", argv[1]);
	char *repeat = argv[2];
	int noescape = 0;
	if (argc > 3) {
		repeat = picolConcatenate(i, " ", argc - 2, argv + 2, 0, -1, 0);
		if (!repeat)
			return PICKLE_ERROR;
		noescape = 1;
	}
	const int escape = !noescape && picolStringNeedsEscaping(repeat);
	const size_t rl  = picolStrlen(repeat);
	char *escaped = escape ? picolEscape(i, repeat, rl) : repeat;
	const size_t el  = escape ? picolStrlen(escaped) : rl;
	char *r = picolMalloc(i, ((el + 1) * count) + 1);
	if (!r)
		goto fail;
	for (long j = 0; j < count; j++) {
		picolMove(r + ((el + 1) * j), escaped, el);
		r[((el + 1) * j) + el] = j < (count - 1) ? ' ' : '\0';
	}
	r[count * el + count] = '\0';
	if (escaped != argv[2])
		(void)picolFree(i, escaped);
	return picolForceResult(i, r, 0);
fail:
	if (escaped != argv[2])
		(void)picolFree(i, escaped);
	(void)picolFree(i, r);
	return PICKLE_ERROR;
}

static inline int picolCommandLLength(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc != 2, "list: get length of a list");
	const picol_args_t a = picolArgs(i, &(pickle_parser_opts_t){ 1, 1, 1, 1 }, argv[1]);
	if (a.argc < 0)
		return PICKLE_ERROR;
	if (picolFreeArgList(i, a.argc, a.argv) != PICKLE_OK)
		return PICKLE_ERROR;
	return picolSetResultNumber(i, a.argc);
}

static inline int picolCommandLReverse(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc != 2, "list: reverse a list");
	const picol_args_t a = picolArgs(i, &(pickle_parser_opts_t){ 1, 1, 1, 1 }, argv[1]);
	if (a.argc < 0)
		return PICKLE_ERROR;
	picolCheck(a.argc >= 0);
	for (int j = 0; j < (a.argc / 2); j++)
		picolSwapString(&a.argv[j], &a.argv[(a.argc - j) - 1]);
	char *s = picolConcatenate(i, " ", a.argc, a.argv, 1, -1, 0);
	if (!s) {
		(void)picolFreeArgList(i, a.argc, a.argv);
		return PICKLE_ERROR;
	}
	if (picolFreeArgList(i, a.argc, a.argv) != PICKLE_OK)
		return PICKLE_ERROR;
	return picolForceResult(i, s, 0);
}

static inline int picolCommandLIndex(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	if (argc == 2)
		return picolSetResultString(i, argv[1]);
	PICOL_ARITY(argc != 3, "list index?: index into a list");
	picol_number_t index = 0;
	if (picolStringToNumber(i, argv[2], &index) != PICKLE_OK)
		return PICKLE_ERROR;
	const picol_args_t a = picolArgs(i, &(pickle_parser_opts_t){ 1, 1, 1, 1 }, argv[1]);
	if (a.argc < 0)
		return PICKLE_ERROR;
	if (!a.argc || index >= a.argc || index < 0) {
		if (picolFreeArgList(i, a.argc, a.argv) != PICKLE_OK)
			return PICKLE_ERROR;
		return picolSetResultEmpty(i);
	}
	index = MAX(0, MIN(index, a.argc - 1));
	const int r1 = picolForceResult(i, a.argv[index], 0);
	a.argv[index] = NULL;
	const int r2 = picolFreeArgList(i, a.argc, a.argv);
	return r1 == PICKLE_OK && r2 == PICKLE_OK ? PICKLE_OK : PICKLE_ERROR;
}

enum { INSERT, DELETE, SET, }; /* picolListOperation, and the list functions, are far too complex... */

static inline int picolListOperation(pickle_t *i, const char *parse, const char *position, int strict, char *insert, int op, int doEsc) {
	picolCheck(i);
	picolCheck(parse);
	picolCheck(position);
	picolCheck(insert);
	const int nogrow = op == SET || op == DELETE;
       	const int escape = doEsc && picolStringNeedsEscaping(insert);
	const size_t il  = picolStrlen(insert);
	picol_number_t index = 0, r = PICKLE_OK;
	if (picolStringToNumber(i, position, &index) != PICKLE_OK)
		return PICKLE_ERROR;
	if (!(insert = escape ? picolEscape(i, insert, il) : insert))
		return PICKLE_ERROR;
	char *prev = NULL;
	picol_args_t a = picolArgs(i, &(pickle_parser_opts_t){ 1, 1, 1, 1 }, parse);
	if (a.argc < 0)
		return PICKLE_ERROR;
	if (strict) {
		if (index < 0 || index > a.argc) {
			r = error(i, "Error number %s", position);
			goto done;
		}
	}
	if (!a.argc) {
		r = picolSetResultString(i, insert);
		goto done;
	}
	index = MAX(0, MIN(index, a.argc - nogrow));
	if (op == INSERT) {
		if (!(a.argv = picolArgsGrow(i, &a.argc, a.argv))) {
			if (escape)
				(void)picolFree(i, insert);
			return PICKLE_ERROR;
		}
		if (index < (a.argc - 1))
			picolMove(&a.argv[index + 1], &a.argv[index], sizeof (*a.argv) * (a.argc - index - 1));
		a.argv[index] = insert;
	} else if (op == SET) {
		picolCheck(index < a.argc);
		prev = a.argv[index];
		a.argv[index] = insert;
	} else {
		picolCheck(op == DELETE && index < a.argc);
		prev = a.argv[index];
		a.argv[index] = NULL;
	}
	if (r == PICKLE_OK) {
		char *s = picolConcatenate(i, " ", a.argc, a.argv, doEsc, -1, 0);
		if (!s)
			r = PICKLE_ERROR;
		else if (picolForceResult(i, s, 0) != PICKLE_OK)
			r = PICKLE_ERROR;
	}
	if (index < a.argc)
		a.argv[index] = prev;
done:
	if (escape)
		if (picolFree(i, insert) != PICKLE_OK)
			r = PICKLE_ERROR;
	if (picolFreeArgList(i, a.argc, a.argv) != PICKLE_OK)
		r = PICKLE_ERROR;
	return r;
}

static inline int picolDoLInsert(pickle_t *i, const char *list, const char *position, const int argc, char **argv, int doEsc, int doEscCat) {
	picolCheck(i);
	picolCheck(list);
	picolCheck(position);
	picolImplies(argc >= 0, argv);
	char *insert = picolConcatenate(i, " ", argc, argv, doEscCat, -1, 0);
	if (!insert)
		return PICKLE_ERROR;
	const int r1 = picolListOperation(i, list, position, 0, insert, INSERT, doEsc);
	const int r2 = picolFree(i, insert);
	return r1 != PICKLE_OK || r2 != PICKLE_OK ? PICKLE_ERROR : PICKLE_OK;
}

static inline int picolCommandLInsert(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc < 4, "list index value: insert a value into a list at an index");
	return picolDoLInsert(i, argv[1], argv[2], argc - 3, argv + 3, 0, 1);
}

static inline int picolCommandLSet(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc != 3 && argc != 4, "variable index value: insert a list into a value at an index");
	if (argc == 3)
		return picolCommandSet(i, argc, argv, pd);
	pickle_var_t *v = picolGetVar(i, argv[1], 1);
	if (!v)
		return error(i, "Error variable %s", argv[1]);
	if (picolListOperation(i, picolGetVarVal(v), argv[2], 1, argv[3], argv[3][0] ? SET : DELETE, 1) != PICKLE_OK)
		return PICKLE_ERROR;
	if (picolFreeVarVal(i, v) != PICKLE_OK)
		return PICKLE_ERROR;
	return picolSetVarString(i, v,  i->result);
}

enum { PICOL_INTEGER, PICOL_STRING, };

static inline int picolOrder(pickle_t *i, int op, int rev, const char *a, const char *b) {
	picolCheck(i);
	picolCheck(a);
	picolCheck(b);
	int r = 0;
	switch (op) {
	case PICOL_INTEGER: {
		picol_number_t an = 0, bn = 0;
		const int ra = picolStringToNumber(i, a, &an);
		const int rb = picolStringToNumber(i, b, &bn);
		if (ra != PICKLE_OK || rb != PICKLE_OK)
			return -1;
		r = an < bn;
		break;
	}
	case PICOL_STRING: {
		r = picolCompare(b, a) > 0;
		break;
	}
	}
	return rev ? !r : r;
}

static inline int picolSortArgs(pickle_t *i, int op, int rev, const int argc, char **argv) {
	picolCheck(i);
	picolCheck(argc >= 0);
	for (int j = 1, k = 0; j < argc; j++) { /* insertion sort */
		char *key = argv[j];
		k = j - 1;
		while (k >= 0) {
			const int od = picolOrder(i, op, rev, key, argv[k]);
			if (od < 0)
				return PICKLE_ERROR;
			if (!od)
				break;
			picolCheck((k + 1) < argc && k < argc);
			argv[k + 1] = argv[k];
			k--;
		}
		picolCheck((k + 1) < argc);
		argv[k + 1] = key;
	}
	return PICKLE_OK;
}

static inline int picolCommandLSort(pickle_t *i, int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc < 2, "-increasing? -decreasing? -ascii? -integer? list: sort a list, increasing/ascii are default");
	int op = PICOL_STRING, rev = 0, j = 1;
	for (j = 1; j < (argc - 1); j++) {
		if (!picolCompare(argv[j], "-increasing"))
			rev = 0;
		else if (!picolCompare(argv[j], "-decreasing"))
			rev = 1;
		else if (!picolCompare(argv[j], "-ascii"))
			op = PICOL_STRING;
		else if (!picolCompare(argv[j], "-integer"))
			op = PICOL_INTEGER;
		else
			return error(i, "Error option %s", argv[j]);
	}
	char *r = NULL;
	picol_args_t a = picolArgs(i, &(pickle_parser_opts_t){ 1, 1, 1, 1 }, argv[j]);
	if (a.argc < 0)
		return PICKLE_ERROR;
	if (picolSortArgs(i, op, rev, a.argc, a.argv) != PICKLE_OK)
		goto fail;
	r = picolConcatenate(i, " ", a.argc, a.argv, 1, -1, 0);
	if (picolFreeArgList(i, a.argc, a.argv) != PICKLE_OK) {
		(void)picolFree(i, r);
		return PICKLE_ERROR;
	}
	if (!r)
		return PICKLE_ERROR;
	return picolForceResult(i, r, 0);
fail:
	(void)picolFree(i, r);
	(void)picolFreeArgList(i, a.argc, a.argv);
	return PICKLE_ERROR;
}

static inline int picolCommandLReplace(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc < 4, "list first last values...: replace a range of values in a list");
	picol_number_t first = 0, last = 0;
	if (picolStringToNumber(i, argv[2], &first) != PICKLE_OK)
		return PICKLE_ERROR;
	if (picolStringToNumber(i, argv[3], &last) != PICKLE_OK)
		return PICKLE_ERROR;
	if (last < first || (first < 0 && last < 0)) {
		char *args = picolConcatenate(i, " ", argc - 4, argv + 4, 1, -1, 0);
		if (!args)
			return PICKLE_ERROR;
		const int r1 = picolDoLInsert(i, argv[1], argv[2], 1, (char *[1]) { args }, 0, 0);
		const int r2 = picolFree(i, args);
		return r1 == PICKLE_OK && r2 == PICKLE_OK ? PICKLE_OK : PICKLE_ERROR;
	}
	first = MAX(0, first);
	int r = 0, empty = 0, used = 0, except = -1;
	picol_args_t a = picolArgs(i, &(pickle_parser_opts_t){ 1, 1, 1, 1 }, argv[1]);
	char *repl = picolConcatenate(i, " ", argc - 4, argv + 4, 1, -1, 0), *n = NULL;
	if ((a.argc < 0) || !repl)
		goto err;
	if (a.argc == 0) {
		if (picolFreeArgList(i, a.argc, a.argv) != PICKLE_OK)
			return PICKLE_ERROR;
		return picolForceResult(i, repl, 0);
	}
	empty = repl[0] == '\0';
	for (size_t j = first; j <= (size_t)last && j < (size_t)a.argc; j++) {
		char *f = a.argv[j];
		a.argv[j] = NULL;
		if (picolFree(i, f) != PICKLE_OK)
			goto err;
		if (j == (size_t)first && !empty) {
			except = j;
			used = 1;
			a.argv[j] = repl;
		}
	}
	if (!(n = picolConcatenate(i, " ", a.argc, a.argv, 1, except, 0)))
		goto err;
	r = picolForceResult(i, n, 0);
	if (!used)
		if (picolFree(i, repl) != PICKLE_OK)
			r = PICKLE_ERROR;
	if (picolFreeArgList(i, a.argc, a.argv) != PICKLE_OK)
		return PICKLE_ERROR;
	return r;
err:
	if (!used)
		(void)picolFree(i, repl);
	(void)picolFreeArgList(i, a.argc, a.argv);
	(void)picolFree(i, n);
	return PICKLE_ERROR;
}

/* implementing the '-all' option would be useful */
static inline int picolCommandLSearch(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc < 3, "-integer? -exact? -inline? -not? -glob? -nocase? [-start number]? list pattern: search a list for a pattern");
	enum { oGLOB, oEXACT, oINTEGER, /*oREGEXP*/ };
	picol_number_t start = 0, value = 0;
	int op = oGLOB, last = argc - 2, index = -1, not = 0, inl = 0, nocase = 0;
	char *list = argv[argc - 2], *pattern = argv[argc - 1];
	for (int j = 1; j < last; j++) {
		     if (!picolCompare(argv[j], "-integer")) { op = oINTEGER; }
		else if (!picolCompare(argv[j], "-exact"))   { op = oEXACT; }
		else if (!picolCompare(argv[j], "-inline"))  { inl = 1; }
		else if (!picolCompare(argv[j], "-nocase"))  { nocase = 1; }
		else if (!picolCompare(argv[j], "-not"))     { not = 1; }
		else if (!picolCompare(argv[j], "-glob"))    { op = oGLOB; }
		else if (!picolCompare(argv[j], "-start")) {
			if (!((j + 1) < last))
				return error(i, "Error option %s", argv[j]);
			j++;
			if (picolStringToNumber(i, argv[j], &start) != PICKLE_OK)
				return PICKLE_ERROR;
		} else {
			return error(i, "Error option %s", argv[j]);
		}
	}
	if (op == oINTEGER)
		if (picolStringToNumber(i, pattern, &value) != PICKLE_OK)
			return PICKLE_ERROR;
	const picol_args_t a = picolArgs(i, &(pickle_parser_opts_t){ 1, 1, 1, 1 }, list);
	if (a.argc < 0)
		return PICKLE_ERROR;
	for (size_t j = start; j < (size_t)a.argc; j++) {
		switch (op) {
		case oGLOB: {
			const int m = picolMatch(pattern, a.argv[j], nocase, PICKLE_MAX_RECURSION - i->level);
			if (m < 0) {
				(void)picolFreeArgList(i, a.argc, a.argv);
				return error(i, "Error recursion %d", m);
			}
			if (not ^ (m > 0)) {
				index = j;
				goto done;
			}
			break;
		}
		case oEXACT:
			if (not ^ (nocase ? !picolCompareCaseInsensitive(pattern, a.argv[j]) : !picolCompare(pattern, a.argv[j]))) {
				index = j;
				goto done;
			}
			break;
		case oINTEGER: {
			picol_number_t n = 0;
			if (picolStringToNumber(i, a.argv[j], &n) != PICKLE_OK) {
				(void)picolFreeArgList(i, a.argc, a.argv);
				return PICKLE_ERROR;
			}
			if (not ^ (n == value)) {
				index = j;
				goto done;
			}
			break;
		}
		default:
			picolCheck(op <= oINTEGER);
		}
	}
done:
	if (inl && index > 0) {
		picolCheck(index < a.argc);
		const int r = picolForceResult(i, a.argv[index], 0);
		a.argv[index] = NULL;
		(void)picolFreeArgList(i, a.argc, a.argv);
		return r;
	}
	(void)picolFreeArgList(i, a.argc, a.argv);
	return picolSetResultNumber(i, index);
}

static inline int picolCommandLRange(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc != 4, "list lower upper: extract a range from a list");
	picol_number_t first = 0, last = 0;
	if (picolStringToNumber(i, argv[2], &first) != PICKLE_OK)
		return PICKLE_ERROR;
	if (picolStringToNumber(i, argv[3], &last) != PICKLE_OK)
		return PICKLE_ERROR;
	if (first > last || (last < 0 && first < 0))
		return picolSetResultEmpty(i);
	first = MAX(0, first);
	last  = MAX(0, last);
	const picol_args_t a = picolArgs(i, &(pickle_parser_opts_t){ 1, 1, 1, 1 }, argv[1]);
	if (a.argc < 0)
		return PICKLE_ERROR;
	last = MIN(last, a.argc - 1);
	if (a.argc == 0 || first > a.argc || (1 + last - first) <= 0) {
		if (picolFreeArgList(i, a.argc, a.argv) != PICKLE_OK)
			return PICKLE_ERROR;
		return picolSetResultEmpty(i);
	}
	char *range = picolConcatenate(i, " ", 1 + last - first, a.argv + first, 1, -1, 0);
	if (!range) {
		(void)picolFreeArgList(i, a.argc, a.argv);
		return PICKLE_ERROR;
	}
	const int rs = picolForceResult(i, range, 0);
	return picolFreeArgList(i, a.argc, a.argv) == PICKLE_OK ? rs : PICKLE_ERROR;
}

static inline int picolCommandLAppend(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc < 2, "variable values...: append values to a list in a variable");
	pickle_var_t *v = picolGetVar(i, argv[1], 1);
	const char *ovar = v ? picolGetVarVal(v) : NULL;
	char *nvar = NULL, *args = picolConcatenate(i, " ", argc - 2, argv + 2, 1, -1, 0);
	if (!args)
		return PICKLE_ERROR;
	char *list[2] = { (char*)ovar, args };
	nvar = picolConcatenate(i, " ", 2 - (ovar == NULL), list + (ovar == NULL), 0, -1, 0);
	const int r1 = picolFree(i, args);
	if (!nvar)
		return PICKLE_ERROR;
	const int r2 = pickle_var_set(i, argv[1], nvar);
	const int r3 = picolForceResult(i, nvar, 0);
	if (r1 != PICKLE_OK && r2 != PICKLE_OK && r3 != PICKLE_OK)
		return PICKLE_ERROR;
	return PICKLE_OK;
}

static inline int picolCommandSplit(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc != 3 && argc != 2, "string splitter?: split a string");
	const char *split = argv[1], *on = argc == 3 ? argv[2] : " ";
	char **nargv = NULL, ch = split[0], *r = NULL;
	int nargc = 0, chars = !*on;
	if (!ch)
		return picolSetResultEmpty(i);
	for (; ch;) {
		size_t j = 0;
		if (chars) { /* special case, split on each character */
			ch = split[j];
			j = 1;
			if (!ch)
				break;
		} else { /* split on character set */
			for (; (ch = split[j]); j++)
				if (picolLocateChar(on, ch))
					break;
		}
		char *t = picolMalloc(i, j + 1);
		if (!t)
			goto fail;
		picolMove(t, split, j);
		split += j + !chars;
		t[j] = '\0';
		char **old = nargv;
		if (!(nargv = picolRealloc(i, nargv, sizeof(*nargv) * (nargc + 1)))) {
			(void)picolFree(i, t);
			nargv = old;
			goto fail;
		}
		nargv[nargc++] = t;
		t = NULL;
	}
	if (!(r = picolConcatenate(i, " ", nargc, nargv, 1, -1, 0)))
		goto fail;
	if (picolFreeArgList(i, nargc, nargv) != PICKLE_OK)
		goto fail;
	return picolForceResult(i, r, 0);
fail:
	(void)picolFree(i, r);
	(void)picolFreeArgList(i, nargc, nargv);
	return PICKLE_ERROR;
}

static int picolCommandRetCodes(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_ARITY(argc != 1, ": control loop word");
	if (pd == (char*)PICKLE_BREAK)
		return PICKLE_BREAK;
	if (pd == (char*)PICKLE_CONTINUE)
		return PICKLE_CONTINUE;
	return PICKLE_OK;
}

static int picolVarFree(pickle_t *i, pickle_var_t *v) {
	if (!v)
		return PICKLE_OK;
	const int r1 = picolFreeVarName(i, v);
	const int r2 = picolFreeVarVal(i, v);
	const int r3 = picolFree(i, v);
	return r1 == PICKLE_OK && r2 == PICKLE_OK && r3 == PICKLE_OK ? PICKLE_OK : PICKLE_ERROR;
}

static int picolDropCallFrame(pickle_t *i) {
	picolCheck(i);
	pickle_call_frame_t *cf = i->callframe;
	picolCheck(i->level >= 0);
	i->level--;
	int r = PICKLE_OK;
	if (!cf)
		return PICKLE_OK;
	pickle_var_t *v = cf->vars, *t = NULL;
	while (v) {
		picolCheck(v != v->next); /* Cycle? */
		t = v->next;
		if (picolVarFree(i, v) != PICKLE_OK)
			r = PICKLE_ERROR;
		v = t;
	}
	i->callframe = cf->parent;
	if (picolFree(i, cf) != PICKLE_OK)
		r = PICKLE_ERROR;
	return r;
}

static int picolDropAllCallFrames(pickle_t *i) {
	picolCheck(i);
	int r = PICKLE_OK;
	while (i->callframe)
		if (picolDropCallFrame(i) != PICKLE_OK)
			r = PICKLE_ERROR;
	return r;
}

static int picolCommandCallProc(pickle_t *i, const int argc, char **argv, void *pd) {
	picolCheck(pd);
	if (i->level > (int)PICKLE_MAX_RECURSION)
		return error(i, "Error recursion %d", PICKLE_MAX_RECURSION);
	char **x = pd, *alist = x[0], *body = x[1], *tofree = NULL;
	char *p = picolStrdup(i, alist);
	int arity = 0, variadic = 0;
	pickle_call_frame_t *cf = picolMalloc(i, sizeof(*cf));
	if (!cf || !p) {
		(void)picolFree(i, p);
		(void)picolFree(i, cf);
		return PICKLE_ERROR;
	}
	cf->vars     = NULL;
	cf->parent   = i->callframe;
	i->callframe = cf;
	i->level++;
	tofree = p;
	for (int done = 0;!done;) {
		const char *start = p;
		while (*p != ' ' && *p != '\0')
			p++;
		if (*p != '\0' && p == start) {
			p++;
			continue;
		}
		if (p == start)
			break;
		if (*p == '\0')
			done = 1;
		else
			*p = '\0';
		if (++arity > (argc - 1)) {
			if (!picolCompare(start, "args")) {
				if (pickle_var_set(i, start, "") != PICKLE_OK)
					goto error;
				variadic = 1;
				break;
			}
			goto arityerr;
		}
		if (done && !picolCompare(start, "args")) {  /* special case: args as last argument */
			variadic = 1;
			char *cat = picolConcatenate(i, " ", argc - arity, &argv[arity], 1, -1, 0);
			if (!cat)
				goto error;
			int r = pickle_var_set(i, start, cat);
			if (picolFree(i, cat) != PICKLE_OK)
				r = PICKLE_ERROR;
			if (r != PICKLE_OK)
				goto error;
		} else {
			if (pickle_var_set(i, start, argv[arity]) != PICKLE_OK)
				goto error;
		}
		p++;
	}
	if (picolFree(i, tofree) != PICKLE_OK)
		goto error;
	tofree = NULL;
	if (!variadic && arity != (argc - 1))
		goto arityerr;
	int errcode = picolEval(i, body);
	if (errcode == PICKLE_RETURN)
		errcode = PICKLE_OK;
	if (picolDropCallFrame(i) != PICKLE_OK)
		return PICKLE_ERROR;
	return errcode;
arityerr:
	(void)error(i, "Error %s arity: %d (wanted %d)", argv[0], argc, arity + 1);
error:
	(void)picolFree(i, tofree);
	(void)picolDropCallFrame(i);
	return PICKLE_ERROR;
}

static int picolCommandAddProc(pickle_t *i, const char *name, const char *args, const char *body) {
	picolCheck(i);
	picolCheck(name);
	picolCheck(args);
	picolCheck(body);
	char **procdata = picolMalloc(i, sizeof(char*)*2);
	if (!procdata)
		return PICKLE_ERROR;
	procdata[0] = picolStrdup(i, args); /* arguments list */
	procdata[1] = picolStrdup(i, body); /* procedure body */
	if (!(procdata[0]) || !(procdata[1])) {
		(void)picolFree(i, procdata[0]);
		(void)picolFree(i, procdata[1]);
		(void)picolFree(i, procdata);
		return PICKLE_ERROR;
	}
	return pickle_command_register(i, name, picolCommandCallProc, procdata);
}

static int picolCommandProc(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc != 4, "name {arg-list} {body}: define a new procedure");
	if (picolGetCommand(i, argv[1]))
		if (picolUnsetCommand(i, argv[1]) != PICKLE_OK)
			return PICKLE_ERROR;
	return picolCommandAddProc(i, argv[1], argv[2], argv[3]);
}

static int picolCommandRename(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc != 3, "old-name new-name: rename a procedure, if new-name is empty it is deleted");
	return pickle_command_rename(i, argv[1], argv[2]);
}

static int picolCommandReturn(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	PICOL_ARITY(argc != 1 && argc != 2 && argc != 3, "string? number?: return a string with a return code");
	picol_number_t r = PICKLE_RETURN;
	if (argc == 3)
		if (picolStringToNumber(i, argv[2], &r) != PICKLE_OK)
			return PICKLE_ERROR;
	if (argc == 1)
		return picolSetResultEmpty(i) != PICKLE_OK ? PICKLE_ERROR : PICKLE_RETURN;
	if (picolSetResultString(i, argv[1]) != PICKLE_OK)
		return PICKLE_ERROR;
	return r;
}

static int picolDoJoin(pickle_t *i, const char *join, const int argc, char **argv, int list, int trim) {
	char *e = picolConcatenate(i, join, argc, argv, list, -1, trim);
	if (!e)
		return PICKLE_ERROR;
	return picolForceResult(i, e, 0);
}

enum { PICOL_CONCAT, PICOL_LIST, PICOL_CONJOIN, };

static int picolCommandConcat(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	switch ((intptr_t)(char*)pd) {
	case PICOL_CONCAT:  return picolDoJoin(i, " ", argc - 1, argv + 1, 0, 1);
	case PICOL_LIST:    return picolDoJoin(i, " ", argc - 1, argv + 1, 1, 0);
	case PICOL_CONJOIN:
		PICOL_ARITY(argc < 2, "string args...: concatenate arguments together with string");
		return picolDoJoin(i, argv[1], argc - 2, argv + 2, 0, 0);
	}
	return PICKLE_ERROR;
}

static int picolCommandJoin(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	PICOL_ARITY(argc != 3, "list string: join a list together with a string");
	const picol_args_t a = picolArgs(i, &(pickle_parser_opts_t){ 1, 1, 1, 1 }, argv[1]);
	if (a.argc < 0)
		return PICKLE_ERROR;
	const int r = picolDoJoin(i, argv[2], a.argc, a.argv, 0, 0);
	return picolFreeArgList(i, a.argc, a.argv) == PICKLE_OK ? r : PICKLE_ERROR;
}

static int picolCommandEval(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	int r = picolDoJoin(i, " ", argc - 1, argv + 1, 0, 0);
	if (r == PICKLE_OK) {
		char *e = picolStrdup(i, i->result);
		if (!e)
			return PICKLE_ERROR;
		r = picolEval(i, e);
		if (picolFree(i, e) != PICKLE_OK)
			r = PICKLE_ERROR;
	}
	return r;
}

static int picolCommandSubst(pickle_t *i, int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	pickle_parser_opts_t o = { 0, 0, 0, 1 };
	int j = 1;
	for (j = 1; j < argc; j++) {
		if (!picolCompare(argv[j], "-nobackslashes"))    { o.noescape = 1;   }
		else if (!picolCompare(argv[j], "-novariables")) { o.novars = 1;     }
		else if (!picolCompare(argv[j], "-nocommands"))  { o.nocommands = 1; }
		else { break; }
	}
	PICOL_ARITY(j >= argc, "-nobackslashes? -novariables? -nocommands? string: optionally perform substitutions on a string");
	return picolEvalAndSubst(i, &o, argv[j]);
}

static int picolSetLevel(pickle_t *i, const int top, int level) { /* NB. Be careful using this function */
	picolCheck(i);
	if (top)
		level = i->level - level;
	if (level < 0)
		return error(i, "Error level %s%d", top ? "#" : "", level);
	for (int j = 0; j < level && i->callframe->parent; j++) {
		picolCheck(i->callframe != i->callframe->parent);
		i->callframe = i->callframe->parent;
	}
	if (level > i->level)
		level = i->level;
	i->level -= level;
	picolCheck(i->level >= 0);
	return PICKLE_OK;
}

static int picolSetLevelByString(pickle_t *i, const char *levelStr) { /* NB. Be careful using this function */
	picolCheck(i);
	picolCheck(levelStr);
	const int top = levelStr[0] == '#';
	picol_number_t level = 0;
	if (picolStringToNumber(i, top ? &levelStr[1] : levelStr, &level) != PICKLE_OK)
		return PICKLE_ERROR;
	return picolSetLevel(i, top, level);
}

static int picolCommandUpVar(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	PICOL_ARITY(argc != 4, "level variable name: create a link to variable in another scope");
	pickle_var_t *m = NULL, *o = NULL;
	pickle_call_frame_t *cf = i->callframe;
	int r = PICKLE_OK, level = i->level;
	if ((r = pickle_var_set(i, argv[3], "")) != PICKLE_OK) {
		(void)error(i, "Error %s %s", argv[0], argv[3]);
		goto end;
	}
	picolCheck(cf);
	m = cf->vars;

	if ((r = picolSetLevelByString(i, argv[1])) != PICKLE_OK)
		goto end;
	if (!(o = picolGetVar(i, argv[2], 1))) {
		if ((r = pickle_var_set(i, argv[2], "")) != PICKLE_OK)
			goto end;
		o = i->callframe->vars;
	}

	if (m == o) { /* more advance cycle detection should be done here */
		r = error(i, "Error %s %s", argv[0], argv[3]);
		goto end;
	}

	m->type = PICOL_PV_LINK;
	m->data.link = o;
end:
	i->level = level;
	i->callframe = cf;
	return r;
}

static int picolCommandUpLevel(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	PICOL_ARITY(argc < 2, "level string...: evaluate a command in another, higher, scope");
	pickle_call_frame_t *cf = i->callframe;
	int level = i->level;
	int r = picolSetLevelByString(i, argv[1]);
	if (r == PICKLE_OK) {
		char *e = picolConcatenate(i, " ", argc - 2, argv + 2, 0, -1, 0);
		if (!e) {
			r = PICKLE_ERROR;
			goto end;
		}
		const int inside_uplevel = i->inside_uplevel ;
		i->inside_uplevel = 1;
		r = picolEval(i, e);
		i->inside_uplevel = inside_uplevel;
		if (picolFree(i, e) != PICKLE_OK)
			r = PICKLE_ERROR;
	}
end:
	i->level = level;
	i->callframe = cf;
	return r;
}

static inline int picolUnsetVar(pickle_t *i, const char *name) {
	picolCheck(i);
	picolCheck(name);
	if (i->inside_uplevel)
		return error(i, "Error operation %s", "unset");
	pickle_call_frame_t *cf = i->callframe;
	pickle_var_t *p = NULL, *deleteMe = picolGetVar(i, name, 0/*NB!*/);
	if (!deleteMe)
		return error(i, "Error variable %s", name);

	if (cf->vars == deleteMe) {
		cf->vars = deleteMe->next;
		return picolVarFree(i, deleteMe);
	}

	for (p = cf->vars; p->next != deleteMe && p; p = p->next)
		;
	picolCheck(p->next == deleteMe);
	p->next = deleteMe->next;
	return picolVarFree(i, deleteMe);
}

static int picolCommandUnSet(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	for (int j = 1; j < argc; j++) /* There's no reason 'unset' could not also work for commands... */
		if (picolUnsetVar(i, argv[j]) != PICKLE_OK)
			return PICKLE_ERROR;
	return PICKLE_OK;
}

enum { PCL_ARGS, PCL_BODY, PCL_PRIVATE, };

static int picolInfoFunction(pickle_t *i, const int type, const char *cmd) {
	picolCheck(i);
	picolCheck(cmd);
	pickle_command_t *c = picolGetCommand(i, cmd);
	if (!c)
		return error(i, "Error command %s", cmd);
	if (type == PCL_PRIVATE)
		return ok(i, "%p", c->privdata);
	picolCheck((type == PCL_BODY || type == PCL_ARGS) && PCL_ARGS == 0 && PCL_BODY == 1);
	const int defined = picolIsDefinedProc(c->func);
	if (!defined) {
		if (type)
			return ok(i, "%p", c->func);
		return ok(i, "built-in");
	}
	char **procdata = c->privdata;
	return picolSetResultString(i, procdata[!!type]);
}

enum { PCL_COMMANDS, PCL_PROCS, PCL_FUNCTIONS, };

static int picolInfoCommands(pickle_t *i, const int type, const char *pat) {
	picolCheck(i);
	picolCheck(pat);
	picolCheck(type == PCL_COMMANDS || type == PCL_PROCS || type == PCL_FUNCTIONS);
	picol_args_t a = { 0, NULL, };
	if (!PICOL_DEFINE_MATHS && type == PCL_FUNCTIONS)
		return picolSetResultEmpty(i);
	for (long j = 0; j < i->length; j++) {
		pickle_command_t *c = i->table[j];
		for (; c; c = c->next) {
			picolCheck(c != c->next);
			if (type == PCL_PROCS && !picolIsDefinedProc(c->func))
				continue;
			if (type == PCL_FUNCTIONS) {
				if (PICOL_DEFINE_MATHS)
					if (c->func != picolCommandMath && c->func != picolCommandMathUnary)
						continue;
			}
			if (picolMatch(pat, c->name, 0, PICKLE_MAX_RECURSION)) {
				if (!(a.argv = picolArgsGrow(i, &a.argc, a.argv)))
					return PICKLE_ERROR;
				a.argv[a.argc - 1] = c->name;
			}
		}
	}
	char *l = picolConcatenate(i, " ", a.argc, a.argv, 1, -1, 0);
	const int r1 = picolFree(i, a.argv); /* NB. Not picolFreeArgList! */
	const int r2 = picolForceResult(i, l, 0);
	return r1 == PICKLE_OK && r2 == PICKLE_OK ? PICKLE_OK : PICKLE_ERROR;
}

static int picolInfoVars(pickle_t *i, const char *pat) {
	picolCheck(i);
	picolCheck(pat);
	picol_args_t a = { 0, NULL, };
	for (pickle_var_t *v = i->callframe->vars; v; v = v->next) {
		char *name = v->smallname ? &v->name.small[0] : v->name.ptr;
		if (v->type == PICOL_PV_LINK)
			continue;
		if (picolMatch(pat, name, 0, PICKLE_MAX_RECURSION)) {
			if (!(a.argv = picolArgsGrow(i, &a.argc, a.argv)))
				return PICKLE_ERROR;
			a.argv[a.argc - 1] = name;
		}
	}
	char *l = picolConcatenate(i, " ", a.argc, a.argv, 1, -1, 0);
	const int r1 = picolFree(i, a.argv); /* NB. Not picolFreeArgList! */
	const int r2 = picolForceResult(i, l, 0);
	return r1 == PICKLE_OK && r2 == PICKLE_OK ? PICKLE_OK : PICKLE_ERROR;
}

static int picolCommandInfo(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	if (argc < 2)
		return error(i, "Error command %s", argv[0]);
	const char *rq = argv[1];
	if (!picolCompare(rq, "commands"))
		return picolInfoCommands(i, PCL_COMMANDS, argc == 2 ? "*" : argv[2]);
	if (!picolCompare(rq, "procs"))
		return picolInfoCommands(i, PCL_PROCS, argc == 2 ? "*" : argv[2]);
	if (!picolCompare(rq, "functions"))
		return picolInfoCommands(i, PCL_FUNCTIONS, argc == 2 ? "*" : argv[2]);
	if (!picolCompare(rq, "locals"))
		return picolInfoVars(i, argc == 2 ? "*" : argv[2]);
	if (!picolCompare(rq, "globals")) {
		int level = i->level, r = PICKLE_ERROR;
		pickle_call_frame_t *cf = i->callframe;
		if (picolSetLevel(i, 1, 0) == PICKLE_OK)
			r = picolInfoVars(i, argc == 2 ? "*" : argv[2]);
		i->callframe = cf;
		i->level = level;
		return r;
	}

	if (!picolCompare(rq, "level"))
		return picolSetResultNumber(i, i->level);
	if (!picolCompare(rq, "cmdcount")) /* For (very rough) code profiling */
		return picolSetResultNumber(i, i->cmdcount);
	if (!picolCompare(rq, "version"))
		return ok(i, "%d %d %d", (int)((PICKLE_VERSION >> 16) & 255),
			(int)((PICKLE_VERSION >> 8) & 255), (int)(PICKLE_VERSION & 255));
	if (argc < 3)
		return error(i, "Error %s %s", argv[0], rq);
	if (!picolCompare(rq, "complete")) {
		pickle_parser_opts_t o = { .noeval = 1, };
		picol_parser_t p = { .p = NULL, };
		int good = 1;
		picolParserInitialize(&p, &o, argv[2]);
		do {
			if (picolGetToken(&p) == PICKLE_ERROR) {
				good = 0;
				break;
			}
		} while (p.type != PCL_EOF);
		return picolForceResult(i, good ? "1" : "0", 1);
	}
	if (!picolCompare(rq, "exists"))
		return picolForceResult(i, picolGetVar(i, argv[2], 0) ? "1" : "0", 1);
	if (!picolCompare(rq, "args"))
		return picolInfoFunction(i, PCL_ARGS, argv[2]);
	if (!picolCompare(rq, "body"))
		return picolInfoFunction(i, PCL_BODY, argv[2]);
	if (!picolCompare(rq, "private"))
		return picolInfoFunction(i, PCL_PRIVATE, argv[2]);
	if (!picolCompare(rq, "system")) {
		static const struct opts { const char *name; picol_number_t info; } opts[] = {
			{ "pointer",   (CHAR_BIT*sizeof(char*)),                  },
			{ "number",    (CHAR_BIT*sizeof(picol_number_t)),         },
			{ "recursion", PICKLE_MAX_RECURSION,                      },
			{ "length",    PICOL_USE_MAX_STRING?PICKLE_MAX_STRING:-1, },
			{ "min",       PICOL_NUMBER_MIN,                          },
			{ "max",       PICOL_NUMBER_MAX,                          },
			{ "string",    PICOL_DEFINE_STRING,                       },
			{ "maths",     PICOL_DEFINE_MATHS,                        },
			{ "list",      PICOL_DEFINE_LIST,                         },
			{ "regex",     PICOL_DEFINE_REGEX,                        },
			{ "help",      PICOL_DEFINE_HELP,                         },
			{ "debugging", PICOL_DEBUGGING,                           },
			{ "strict",    PICOL_STRICT_NUMERIC_CONVERSION,           },
		};
		for (size_t j = 0; j < (sizeof (opts) / sizeof (opts[0])); j++)
			if (!picolCompare(opts[j].name, argv[2]))
				return picolSetResultNumber(i, opts[j].info);
		if (!picolCompare(argv[2], "license"))
			return picolForceResult(i, PICKLE_LICENSE, 1);
		if (!picolCompare(argv[2], "email"))
			return picolForceResult(i, PICKLE_EMAIL, 1);
		if (!picolCompare(argv[2], "repo"))
			return picolForceResult(i, PICKLE_REPO, 1);
		if (!picolCompare(argv[2], "author"))
			return picolForceResult(i, PICKLE_AUTHOR, 1);
	}
	return error(i, "Error %s %s", argv[0], rq);
}

static int picolCommandTrace(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	PICOL_ARITY(argc != 2, "on|off|status: trace function execution");
	if (!picolCompare(argv[1], "on")) {
		i->trace = 1;
		return PICKLE_OK;
	}
	if (!picolCompare(argv[1], "off")) {
		i->trace = 0;
		return PICKLE_OK;
	}
	if (!picolCompare(argv[1], "status"))
		return picolForceResult(i, i->trace ? "1" : "0", 1);
	return error(i, "Error %s %s", argv[0], argv[1]);
}

/* Regular Expression Engine
 * Modified from:
 * https://www.cs.princeton.edu/courses/archive/spr09/cos333/beautiful.html
 *
 * Supports: "^$.*+?", escaping, and classes "\w\W\s\S\d\D"
 * Nice to have: hex escape sequences, ability to work on binary data. */

enum { /* watch out for those negatives */
	PCL_BEGIN =  '^', PCL_RESC   =  '\\', PCL_REOI  = '\0',
	PCL_END   = -'$', PCL_ANY    = -'.', PCL_MANY = -'*', PCL_ATLEAST = -'+', PCL_MAYBE  = -'?',
	PCL_ALPHA = -'w', PCL_NALPHA = -'W',
	PCL_DIGIT = -'d', PCL_NDIGIT = -'D',
	PCL_SPACE = -'s', PCL_NSPACE = -'S',
};

enum { PCL_LAZY, PCL_GREEDY, PCL_POSSESSIVE, };

/* escape a character, or return an operator */
static int picolRegexEscape(const unsigned ch, const int esc) {
	switch (ch) {
	case -PCL_END: case -PCL_ANY: case -PCL_MANY: case -PCL_ATLEAST: case -PCL_MAYBE:
		return esc ? ch : -ch;
	case 'w': return esc ? PCL_ALPHA  : 'w';
	case 'W': return esc ? PCL_NALPHA : 'W';
	case 'd': return esc ? PCL_DIGIT  : 'd';
	case 'D': return esc ? PCL_NDIGIT : 'D';
	case 's': return esc ? PCL_SPACE  : 's';
	case 'S': return esc ? PCL_NSPACE : 'S';
	case 'a': return esc ? '\a'   : 'a';
	case 'b': return esc ? '\b'   : 'b';
	case 'e': return esc ?  27    : 'e';
	case 'f': return esc ? '\f'   : 'f';
	case 'n': return esc ? '\n'   : 'n';
	case 'r': return esc ? '\r'   : 'r';
	case 't': return esc ? '\t'   : 't';
	case 'v': return esc ? '\v'   : 'v';
	case PCL_BEGIN: case PCL_RESC: case PCL_REOI: break;
	}
	return ch;
}

static int picolRegexChar(const pickle_regex_t *x, const int pattern, const int ch) {
	picolCheck(x);
	switch (pattern) {
	case PCL_ANY:   return 1;
	case PCL_ALPHA: return isalpha(ch); case PCL_NALPHA: return !isalpha(ch);
	case PCL_DIGIT: return isdigit(ch); case PCL_NDIGIT: return !isdigit(ch);
	case PCL_SPACE: return isspace(ch); case PCL_NSPACE: return !isspace(ch);
	}
	if (x->nocase)
		return tolower(pattern) == tolower(ch);
	return pattern == ch;
}

static int picolRegexStar(pickle_regex_t *x, int depth, int c, const char *regexp, const char *text);

/* search for regexp at beginning of text */
static int picolRegexHere(pickle_regex_t *x, const int depth, const char *regexp, const char *text) {
	picolCheck(x);
	picolCheck(regexp);
	picolCheck(text);
	if (x->max && depth > x->max)
		return -1;
	int r1 = PCL_REOI, r2 = PCL_REOI;
again:
	r1 = picolRegexEscape(regexp[0], 0);
	if (r1 == PCL_REOI) {
		x->end = text;
		return 1;
	}
	if (r1 == PCL_BEGIN)
		return -1;
	if (r1 == PCL_RESC) {
		r1 = picolRegexEscape(regexp[1], 1);
		if (r1 == PCL_REOI)
			return -1;
		regexp++;
	}
	r2 = picolRegexEscape(regexp[1], 0);
	if (r2 == PCL_MAYBE) {
		const int is = picolRegexChar(x, r1, *text);
		if (x->type == PCL_GREEDY) {
			if (is) {
				const int m = picolRegexHere(x, depth + 1, regexp + 2, text + 1);
				if (m)
					return m;
			}
			regexp += 2;
			goto again;
		} else if (x->type == PCL_LAZY) {
			const int m = picolRegexHere(x, depth + 1, regexp + 2, text);
			if (m)
				return m;
			if (!is)
				return 0;
			regexp += 2, text++;
			goto again;
		} else {
			picolCheck(x->type == PCL_POSSESSIVE);
			regexp += 2, text += is;
			goto again;
		}
	}
	if (r2 == PCL_ATLEAST) {
		if (!picolRegexChar(x, r1, *text))
			return 0;
		return picolRegexStar(x, depth + 1, r1, regexp + 2, text + 1);
	}
	if (r2 == PCL_MANY)
		return picolRegexStar(x, depth + 1, r1, regexp + 2, text);
	if (r1 == PCL_END) {
		if (r2 != PCL_REOI)
			return -1;
		const int m = *text == PCL_REOI;
		x->end = m == 1 ? text : NULL;
		return m;
	}
	if (*text != PCL_REOI && picolRegexChar(x, r1, *text)) {
		regexp++, text++;
		goto again;
	}
	return 0;
}

/* search for c*regexp at beginning of text */
static int picolRegexStar(pickle_regex_t *x, const int depth, const int c, const char *regexp, const char *text) {
	picolCheck(x);
	picolCheck(regexp);
	picolCheck(text);
	if (x->max && depth > x->max)
		return -1;
	if (x->type == PCL_GREEDY || x->type == PCL_POSSESSIVE) {
		const char *t = NULL;
		for (t = text; *t != PCL_REOI && picolRegexChar(x, c, *t); t++)
			;
		if (x->type == PCL_POSSESSIVE)
			return picolRegexHere(x, depth + 1, regexp, t);
		do {
			const int m = picolRegexHere(x, depth + 1, regexp, t);
			if (m)
				return m;
		} while (t-- > text);
		return 0;
	}
	picolCheck(x->type == PCL_LAZY);
	do {    /* a '*' matches zero or more instances */
		const int m = picolRegexHere(x, depth + 1, regexp, text);
		if (m)
			return m;
	} while (*text != PCL_REOI && picolRegexChar(x, c, *text++));
	return 0;
}

/* search for regexp anywhere in text */
static int picolRegexExtract(pickle_regex_t *x, const char *regexp, const char *text) {
	picolCheck(x);
	picolCheck(regexp);
	picolCheck(text);
	x->start = NULL;
	x->end   = NULL;
	const int start = regexp[0] == PCL_BEGIN;
	do {    /* must look even if string is empty */
		const int m = picolRegexHere(x, 0, regexp + start, text);
		if (m || start) {
			if (m > 0)
				x->start = text;
			return m;
		}
	} while (*text++ != PCL_REOI);
	x->start = NULL;
	x->end   = NULL;
	return 0;
}

static inline int picolCommandRegex(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	picolCheck(!pd);
	PICOL_ARITY(argc < 3, "-nocase? -possessive? -lazy? -greedy? [-start number]? regex string: match a regex on a string");
	picol_number_t index = 0;
	unsigned type = PCL_GREEDY, nocase = 0;
	const int last = argc - 2;
	for (int j = 1; j < last; j++) {
		if (!picolCompare(argv[j], "-nocase"))          { nocase = 1; }
		else if (!picolCompare(argv[j], "-possessive")) { type = PCL_POSSESSIVE; }
		else if (!picolCompare(argv[j], "-lazy"))       { type = PCL_LAZY; }
		else if (!picolCompare(argv[j], "-greedy"))     { type = PCL_GREEDY; }
		else if (!picolCompare(argv[j], "-start")) {
			if (!((j + 1) < last))
				return error(i, "Error %s %s", argv[0], argv[j]);
			j++;
			if (picolStringToNumber(i, argv[j], &index) != PICKLE_OK)
				return PICKLE_ERROR;
		} else {
			return error(i, "Error %s %s", argv[0], argv[j]);
		}
	}
	const char *pattern = argv[last], *string = argv[last + 1], *orig = argv[last + 1];
	if (index) {
		const size_t l = picolStrlen(string);
		if (index < 0)
			index = 0;
		if ((size_t)index > l)
			index = l;
		string += index;
	}
	pickle_regex_t x = { NULL, NULL, PICKLE_MAX_RECURSION, type, nocase, };
	const int r = picolRegexExtract(&x, pattern, string);
	picolMutual(x.start, x.end);
	if (r < 0)
		return error(i, "Error %s %s", argv[0], pattern);
	if (r == 0)
		return picolSetResultString(i, "-1 -1");
	picolCheck(x.start);
	picolCheck(x.end);
	picolImplies(x.start, x.start >= orig);
	picolImplies(x.end,   x.end   >= x.start);
	picol_number_t start = x.start - orig, end = x.end - orig;
	end -= (end != start);
	return ok(i, "%ld %ld", (long)start, (long)end);
}

static int picolRegisterCoreCommands(pickle_t *i) {
	picolCheck(i);

	typedef PICOL_PREPACK struct {
		const char *name;   /* Name of function/TCL command */
		pickle_func_t func; /* Callback that actually does stuff */
		void *data;         /* Optional data for this function */
	} PICOL_POSTPACK pickle_command_register_t; /* A single TCL command */

	/* To save on RAM a static lookup table could be made for built in
	 * commands instead of adding them to the hash table. Being able
	 * to 'rename' any function would complicate this however. */
	static const pickle_command_register_t commands[] = {
		{ "apply",     picolCommandApply,     NULL, },
		{ "break",     picolCommandRetCodes,  (char*)PICKLE_BREAK, },
		{ "catch",     picolCommandCatch,     NULL, },
		{ "concat",    picolCommandConcat,    (char*)PICOL_CONCAT, },
		{ "conjoin",   picolCommandConcat,    (char*)PICOL_CONJOIN, },
		{ "continue",  picolCommandRetCodes,  (char*)PICKLE_CONTINUE, },
		{ "eq",        picolCommandEqual,     NULL, },
		{ "eval",      picolCommandEval,      NULL, },
		{ "for",       picolCommandFor,       NULL, },
		{ "if",        picolCommandIf,        NULL, },
		{ "incr",      picolCommandIncr,      NULL, },
		{ "info",      picolCommandInfo,      NULL, },
		{ "join",      picolCommandJoin,      NULL, },
		{ "list",      picolCommandConcat,    (char*)PICOL_LIST, },
		{ "ne",        picolCommandNotEqual,  NULL, },
		{ "proc",      picolCommandProc,      NULL, },
		{ "rename",    picolCommandRename,    NULL, },
		{ "return",    picolCommandReturn,    NULL, },
		{ "set",       picolCommandSet,       NULL, },
		{ "subst",     picolCommandSubst,     NULL, },
		{ "trace",     picolCommandTrace,     NULL, },
		{ "unset",     picolCommandUnSet,     NULL, },
		{ "uplevel",   picolCommandUpLevel,   NULL, },
		{ "upvar",     picolCommandUpVar,     NULL, },
		{ "while",     picolCommandWhile,     NULL, },
#if PICOL_DEFINE_LIST
		{ "lappend",   picolCommandLAppend,   NULL, },
		{ "lindex",    picolCommandLIndex,    NULL, },
		{ "linsert",   picolCommandLInsert,   NULL, },
		{ "llength",   picolCommandLLength,   NULL, },
		{ "lrange",    picolCommandLRange,    NULL, },
		{ "lrepeat",   picolCommandLRepeat,   NULL, },
		{ "lreplace",  picolCommandLReplace,  NULL, },
		{ "lreverse",  picolCommandLReverse,  NULL, },
		{ "lsearch",   picolCommandLSearch,   NULL, },
		{ "lset",      picolCommandLSet,      NULL, },
		{ "lsort",     picolCommandLSort,     NULL, },
		{ "split",     picolCommandSplit,     NULL, },
#endif
#if PICOL_DEFINE_REGEX
		{ "reg",       picolCommandRegex,     NULL, },
#endif
#if PICOL_DEFINE_STRING
		{ "string",    picolCommandString,    NULL, },
#endif
#if PICOL_DEFINE_MATHS
		{ "!=",        picolCommandMath,      (char*)BNEQ,     },
		{ "&",         picolCommandMath,      (char*)BAND,     },
		{ "&&",        picolCommandMath,      (char*)BLAND,    },
		{ "*",         picolCommandMath,      (char*)BMUL,     },
		{ "+",         picolCommandMath,      (char*)BADD,     },
		{ "-",         picolCommandMath,      (char*)BSUB,     },
		{ "/",         picolCommandMath,      (char*)BDIV,     },
		{ "<",         picolCommandMath,      (char*)BLESS,    },
		{ "<=",        picolCommandMath,      (char*)BLEQ,     },
		{ "==",        picolCommandMath,      (char*)BEQ,      },
		{ ">",         picolCommandMath,      (char*)BMORE,    },
		{ ">=",        picolCommandMath,      (char*)BMEQ,     },
		{ "^",         picolCommandMath,      (char*)BXOR,     },
		{ "and",       picolCommandMath,      (char*)BAND,     },
		{ "log",       picolCommandMath,      (char*)BLOG,     },
		{ "lshift",    picolCommandMath,      (char*)BLSHIFT,  },
		{ "max",       picolCommandMath,      (char*)BMAX,     },
		{ "min",       picolCommandMath,      (char*)BMIN,     },
		{ "mod",       picolCommandMath,      (char*)BMOD,     },
		{ "or",        picolCommandMath,      (char*)BOR,      },
		{ "pow",       picolCommandMath,      (char*)BPOW,     },
		{ "rshift",    picolCommandMath,      (char*)BRSHIFT,  },
		{ "xor",       picolCommandMath,      (char*)BXOR,     },
		{ "|",         picolCommandMath,      (char*)BOR,      },
		{ "||",        picolCommandMath,      (char*)BLOR,     },
		{ "!",         picolCommandMathUnary, (char*)UNOT,     },
		{ "abs",       picolCommandMathUnary, (char*)UABS,     },
		{ "bool",      picolCommandMathUnary, (char*)UBOOL,    },
		{ "invert",    picolCommandMathUnary, (char*)UINV,     },
		{ "negate",    picolCommandMathUnary, (char*)UNEGATE,  },
		{ "not",       picolCommandMathUnary, (char*)UNOT,     },
		{ "~",         picolCommandMathUnary, (char*)UINV,     },
#endif
	};
	for (size_t j = 0; j < sizeof(commands)/sizeof(commands[0]); j++)
		if (picolRegisterCommand(i, commands[j].name, commands[j].func, commands[j].data) != PICKLE_OK)
			return PICKLE_ERROR;
	return PICKLE_OK;
}

static int picolFreeCmd(pickle_t *i, pickle_command_t *p) {
	picolCheck(i);
	if (!p)
		return PICKLE_OK;
	int r = PICKLE_OK;
	if (picolIsDefinedProc(p->func)) {
		char **procdata = (char**) p->privdata;
		if (procdata) {
			if (picolFree(i, procdata[0]) != PICKLE_OK)
				r = PICKLE_ERROR;
			if (picolFree(i, procdata[1]) != PICKLE_OK)
				r = PICKLE_ERROR;
		}
		if (picolFree(i, procdata) != PICKLE_OK)
			r = PICKLE_ERROR;
	}
	if (picolFree(i, p->name) != PICKLE_OK)
		r = PICKLE_ERROR;
	if (picolFree(i, p) != PICKLE_OK)
		r = PICKLE_ERROR;
	return r;
}

static int picolDeinitialize(pickle_t *i) {
	picolCheck(i);
	int r = picolDropAllCallFrames(i);
	picolCheck(!(i->callframe));
	if (picolFreeResult(i) != PICKLE_OK)
		r = PICKLE_ERROR;
	for (long j = 0; j < i->length; j++) {
		pickle_command_t *c = i->table[j], *p = NULL;
		for (; c; p = c, c = c->next) {
			if (picolFreeCmd(i, p) != PICKLE_OK)
				r = PICKLE_ERROR;
			picolCheck(c != c->next);
		}
		if (picolFreeCmd(i, p) != PICKLE_OK)
			r = PICKLE_ERROR;
	}
	if (picolFree(i, i->table) != PICKLE_OK)
		r = PICKLE_ERROR;
	picolZero(i, sizeof *i);
	i->fatal = 1;
	return r;
}

static int picolInitialize(pickle_t *i, allocator_fn fn, void *arena) {
	static_assertions();
	picolCheck(i);
	picolCheck(fn);
	/*'i' may contain junk, otherwise: picolCheck(!(i->initialized));*/
	const size_t hbytes = PICKLE_MAX_STRING;
	const size_t helem  = hbytes / sizeof (*i->table);
	picolZero(i, sizeof *i);
	i->initialized   = 1;
	i->allocator     = fn;
	i->arena         = arena;
	i->callframe     = i->allocator(i->arena, NULL, 0, sizeof(*i->callframe));
	i->result        = picol_string_empty;
	i->static_result = 1;
	i->table         = picolMalloc(i, hbytes); /* NB. We could make this configurable, for little gain. */

	if (!(i->callframe) || !(i->result) || !(i->table))
		goto fail;
	picolZero(i->table,     hbytes);
	picolZero(i->callframe, sizeof(*i->callframe));
	i->length = helem;
	if (picolRegisterCoreCommands(i) != PICKLE_OK)
		goto fail;
	if (pickle_var_set(i, "argv", "") != PICKLE_OK)
		goto fail;
	return PICKLE_OK;
fail:
	(void)picolDeinitialize(i);
	return PICKLE_ERROR;
}

static inline int picolTest(allocator_fn fn, void *arena, const char *eval, const char *result, int retcode) {
	picolCheck(fn);
	picolCheck(eval);
	picolCheck(result);
	int r = 0, actual = 0;
	pickle_t *p = NULL;
	const int rc = pickle_new(&p, fn, arena);
	if (rc != PICKLE_OK || !p)
		return -1;
	if ((actual = picolEval(p, eval)) != retcode) { r = -2; goto end; }
	if (!(p->result))                             { r = -3; goto end; }
	if (picolCompare(p->result, result))               { r = -4; goto end; }
end:
	if (pickle_delete(p) != PICKLE_OK)
		return -1;
	return r;
}

static inline int picolTestSmallString(allocator_fn fn, void *arena) {
	PICOL_UNUSED(fn);
	PICOL_UNUSED(arena);
	int r = 0;
	if (!picolIsSmallString(""))  { r = -1; }
	if (!picolIsSmallString("0")) { r = -2; }
	if (picolIsSmallString("Once upon a midnight dreary")) { r = -3; }
	return r;
}

static inline int picolTestUnescape(allocator_fn fn, void *arena) {
	PICOL_UNUSED(fn);
	PICOL_UNUSED(arena);
	int r = 0;
	char m[256];
	static const struct unescape_results {
		char *str;
		char *res;
		int r;
	} ts[] = {
		{  "",              "",       0   },
		{  "a",             "a",      1   },
		{  "\\t",           "\t",     1   },
		{  "\\ta",          "\ta",    2   },
		{  "a\\[",          "a[",     2   },
		{  "a\\[\\[",       "a[[",    3   },
		{  "a\\[z\\[a",     "a[z[a",  5   },
		{  "\\\\",          "\\",     1   },
		{  "\\x30",         "0",      1   },
		{  "\\xZ",          "N/A",    -2  },
		{  "\\xZZ",         "N/A",    -2  },
		{  "\\x9",          "\x09",   1   },
		{  "\\x9Z",         "\011Z",  2   },
		{  "\\x300",        "00",     2   },
		{  "\\x310",        "10",     2   },
		{  "\\x31\\x312",   "112",    3   },
		{  "x\\x31\\x312",  "x112",   4   },
	};

	for (size_t i = 0; i < sizeof(ts)/sizeof(ts[0]); i++) {
		picolZero(m, sizeof m); /* lazy */
		strncpy(m, ts[i].str, sizeof(m) - 1);
		const int u = picolUnEscape(m, picolStrlen(m) + 1);
		if (ts[i].r != u) {
			r = -1;
			continue;
		}
		if (u < 0)
			continue;
		if (picolCompare(m, ts[i].res)) {
			r = -2;
			continue;
		}
	}
	return r;
}

static inline int picolConcatenateTest(pickle_t *i, const char *result, const char *join, int argc, char **argv) {
	int r = PICKLE_OK;
	char *f = NULL;
	if (!(f = picolConcatenate(i, join, argc, argv, 0, -1, 0)) || picolCompare(f, result))
		r = PICKLE_ERROR;
	return picolFree(i, f) == PICKLE_OK ? r : PICKLE_ERROR;
}

static inline int picolTestConcat(allocator_fn fn, void *arena) {
	picolCheck(fn);
	int r = 0;
	pickle_t *p = NULL;
	if (pickle_new(&p, fn, arena) != PICKLE_OK || !p)
		return -100;
	r += picolConcatenateTest(p, "ac",    "",  2, (char*[2]){"a", "c"});
	r += picolConcatenateTest(p, "a,c",   ",", 2, (char*[2]){"a", "c"});
	r += picolConcatenateTest(p, "a,b,c", ",", 3, (char*[3]){"a", "b", "c"});
	r += picolConcatenateTest(p, "a",     "X", 1, (char*[1]){"a"});
	r += picolConcatenateTest(p, "",      "",  0, NULL);

	if (pickle_delete(p) != PICKLE_OK)
		r = -10;
	return r;
}

static inline int picolTestEval(allocator_fn fn, void *arena) {
	static const struct test_t {
		int retcode;
		char *eval, *result;
	} ts[] = { /* More tests would be nice. */
		{ PICKLE_OK,     "+  2 2",          "4",     },
		{ PICKLE_OK,     "* -2 9",          "-18",   },
		{ PICKLE_OK,     "join {a b c} ,",  "a,b,c", },
		{ PICKLE_ERROR,  "return fail -1",  "fail",  },
	};

	int r = 0;
	for (size_t i = 0; i < sizeof(ts)/sizeof(ts[0]); i++)
		if (picolTest(fn, arena, ts[i].eval, ts[i].result, ts[i].retcode) < 0)
			r = -(int)(i+1);
	return r;
}

static inline int picolTestConvertNumber(allocator_fn fn, void *arena) {
	picolCheck(fn);
	int r = 0;
	pickle_t *p = NULL;

	static const struct test_t {
		picol_number_t val;
		int error;
		char *string;
	} ts[] = {
		{   0, PICKLE_ERROR, ""      },
		{   0, PICKLE_OK,    "0"     },
		{   1, PICKLE_OK,    "1"     },
		{  -1, PICKLE_OK,    "-1"    },
		{ 123, PICKLE_OK,    "123"   },
		{   0, PICKLE_ERROR, "+-123" },
		{   0, PICKLE_ERROR, "-+123" },
		{   0, PICKLE_ERROR, "-+123" },
		{   4, PICKLE_OK,    "+4"    },
		{   0, PICKLE_ERROR, "4x"    },
	};

	if (pickle_new(&p, fn, arena) != PICKLE_OK || !p)
		return -1;

	for (size_t i = 0; i < sizeof(ts)/sizeof(ts[0]); i++) {
		const struct test_t *t = &ts[i];
		picol_number_t val = 0;
		const int error = picolStringToNumber(p, t->string, &val);
		const int fail = (t->val != val || t->error != error);
		if (fail)
			r = -2;
	}

	if (pickle_delete(p) != PICKLE_OK)
		r = -3;
	return r;
}

static inline int picolTestGetSetVar(allocator_fn fn, void *arena) {
	picolCheck(fn);
	const char *val = 0;
	int r = 0;
	pickle_t *p = NULL;
	if (pickle_new(&p, fn, arena) != PICKLE_OK || !p)
		return -1;
	r += (pickle_eval(p, "set a 54; set b 3; set c -4x") != PICKLE_OK);
	r += (pickle_var_get(p, "a", &val) != PICKLE_OK || picolCompare(val, "54"));
	r += (pickle_var_get(p, "c", &val) != PICKLE_OK || picolCompare(val, "-4x"));
	r += (pickle_var_set(p, "d", "123") != PICKLE_OK);
	r += (pickle_var_get(p, "d", &val) != PICKLE_OK || picolCompare(val, "123"));
	r += (pickle_delete(p) != PICKLE_OK);
	return -r;
}

static inline int picolTestParser(allocator_fn fn, void *arena) {
	PICOL_UNUSED(fn);
	PICOL_UNUSED(arena);
	int r = 0;
	picol_parser_t p = { .p = NULL, };

	static const struct test_t { char *text; } ts[] = {
		{ "$a", },
		{ "\"a b c\"", },
		{ "a  b c {a b c}", },
		{ "[+ 2 2]", },
		{ "[+ 2 2]; $a; {v}", },
	};

	for (size_t i = 0; i < (sizeof(ts) / sizeof(ts[0])); i++) {
		picolParserInitialize(&p, NULL, ts[i].text);
		do {
			if (picolGetToken(&p) == PICKLE_ERROR)
				break;
			picolCheck(p.start && p.end);
			picolCheck(p.type <= PCL_EOF);
		} while (p.type != PCL_EOF);
	}
	return r;
}

static int picolTestRegex(allocator_fn fn, void *arena) {
	PICOL_UNUSED(fn);
	PICOL_UNUSED(arena);
	int r = 0;
	struct tests { int match; char *reg, *str; } ts[] = {
		{ 1,  "a",      "bba",      }, { 1,  ".",      "x",        },
		{ 1,  "\\.",    ".",        }, { 0,  "\\.",    "x",        },
		{ 0,  ".",      "",         }, { 0,  "a",      "b",        },
		{ 1,  "^a*b$",  "b",        }, { 0,  "^a*b$",  "bx",       },
		{ 1,  "a*b",    "b",        }, { 1,  "a*b",    "ab",       },
		{ 1,  "a*b",    "aaaab",    }, { 1,  "a*b",    "xaaaab",   },
		{ 0,  "^a*b",   "xaaaab",   }, { 1,  "a*b",    "xaaaabx",  },
		{ 1,  "a*b",    "xaaaaxb",  }, { 0,  "a*b",    "xaaaax",   },
		{ 0,  "a$",     "ab",       }, { 1,  "a*",     "",         },
		{ 1,  "a*",     "a",        }, { 1,  "a*",     "aa",       },
		{ 1,  "a+",     "a",        }, { 0,  "a+",     "",         },
		{ 1,  "ca?b",   "cab",      }, { 1,  "ca?b",   "cb",       },
		{ 1,  "\\sz",   " \t\r\nz", }, { 0,  "\\s",    "x",        },
	};
	for (size_t j = 0; j < (sizeof (ts) / sizeof (ts[0])); j++) {
		pickle_regex_t x = { NULL, NULL, PICKLE_MAX_RECURSION, PCL_LAZY, 0, };
		if (ts[j].match != picolRegexExtract(&x, ts[j].reg, ts[j].str))
			r--;
	}
	return -r;
}

static int picolSetResultArgError(pickle_t *i, const unsigned line, const char *comp, const char *help, const int argc, char **argv) {
	picolPre(i);
	picolCheck(argv);
	picolCheck(comp);
	picolCheck(help);
	picolCheck(argc >= 1);
	PICOL_UNUSED(line);
	char *as = picolConcatenate(i, " ", argc, argv, 1, -1, 0);
	if (!as)
		return picolPost(i, PICKLE_ERROR);
	if (PICOL_DEFINE_HELP)
		(void)error(i, "Error arguments (%s) {help %s %s} got -> %s", comp, argv[0], help, as);
	else
		(void)error(i, "Error arguments %s", as);
	(void)picolFree(i, as);
	return picolPost(i, PICKLE_ERROR);
}

int pickle_result_get(pickle_t *i, const char **s) {
	picolPre(i);
	picolCheck(s);
	*s = i->result;
	return picolPost(i, PICKLE_OK);
}

int pickle_command_register(pickle_t *i, const char *name, pickle_func_t func, void *privdata) {
	picolPre(i);
	picolCheck(name);
	picolCheck(func);
	return picolPost(i, picolRegisterCommand(i, name, func, privdata));
}

int pickle_var_set(pickle_t *i, const char *name, const char *val) {
	picolPre(i);
	picolCheck(name);
	picolCheck(val);
	int r = PICKLE_OK;
	pickle_var_t *v = picolGetVar(i, name, 1);
	if (v) {
		r = picolFreeVarVal(i, v);
		if (picolSetVarString(i, v, val) != PICKLE_OK)
			return picolPost(i, PICKLE_ERROR);
	} else {
		if (!(v = picolMalloc(i, sizeof(*v))))
			return picolPost(i, PICKLE_ERROR);
		picolZero(v, sizeof *v);
		const int r1 = picolSetVarName(i, v, name);
		const int r2 = picolSetVarString(i, v, val);
		if (r1 != PICKLE_OK || r2 != PICKLE_OK) {
			(void)picolFreeVarName(i, v);
			(void)picolFreeVarVal(i, v);
			(void)picolFree(i, v);
			return picolPost(i, PICKLE_ERROR);
		}
		v->next = i->callframe->vars;
		i->callframe->vars = v;
	}
	return picolPost(i, r);
}

int pickle_var_get(pickle_t *i, const char *name, const char **val) {
	picolPre(i);
	picolCheck(name);
	picolCheck(val);
	*val = NULL;
	pickle_var_t *v = picolGetVar(i, name, 1);
	if (!v)
		return picolPost(i, PICKLE_ERROR);
	*val = picolGetVarVal(v);
	return picolPost(i, *val ? PICKLE_OK : PICKLE_ERROR);
}

int pickle_eval_args(pickle_t *i, int argc, char **argv) {
	picolPre(i);
	picolCheck(argc >= 0);
	picolCheck(argv);
	char *c = picolConcatenate(i, " ", argc, argv, 1, -1, 0);
	if (!c)
		return picolPost(i, PICKLE_ERROR);
	const int r1 = pickle_eval(i, c);
	const int r2 = picolFree(i, c);
	return r2 == PICKLE_OK ? r1 : PICKLE_ERROR;
}

int pickle_result_set(pickle_t *i, const int ret, const char *fmt, ...) {
	picolPre(i);
	picolCheck(fmt);
	va_list ap;
	if (fmt[0] == '\0')
		return picolPost(i, picolSetResultEmpty(i));
	if (fmt[0] == '%' && fmt[1] == 's' && fmt[2] == '\0') {
		va_start(ap, fmt);
		const int r = picolSetResultString(i, va_arg(ap, char*));
		va_end(ap);
		return picolPost(i, r);
	}
	va_start(ap, fmt);
	char *r = picolVsprintf(i, fmt, ap);
	va_end(ap);
	if (!r) {
		(void)picolSetResultString(i, "Error vsnprintf");
		return picolPost(i, PICKLE_ERROR);
	}
	if (picolForceResult(i, r, 0) != PICKLE_OK)
		return picolPost(i, PICKLE_ERROR);
	return picolPost(i, ret);
}

int pickle_eval(pickle_t *i, const char *t) {
	picolPre(i);
	picolCheck(t);
	if (i->fatal)
		return PICKLE_ERROR;
	return picolEval(i, t); /* may return any int */
}

int pickle_command_rename(pickle_t *i, const char *src, const char *dst) {
	picolPre(i);
	picolCheck(src);
	picolCheck(dst);
	if (picolGetCommand(i, dst))
		return picolPost(i, error(i, "Error operation %s", dst));
	if (!picolCompare(dst, picol_string_empty))
		return picolPost(i, picolUnsetCommand(i, src));
	pickle_command_t *np = picolGetCommand(i, src);
	if (!np)
		return picolPost(i, error(i, "Error command %s", src));
	int r = PICKLE_ERROR;
	if (picolIsDefinedProc(np->func)) {
		char **procdata = (char**)np->privdata;
		r = picolCommandAddProc(i, dst, procdata[0], procdata[1]);
	} else {
		r = pickle_command_register(i, dst, np->func, np->privdata);
	}
	if (r != PICKLE_OK)
		return picolPost(i, r);
	return picolPost(i, picolUnsetCommand(i, src));
}

int pickle_allocator_get(pickle_t *i, allocator_fn *fn, void **arena) {
	picolCheck(arena);
	picolCheck(fn);
	picolPre(i);
	*fn = i->allocator;
	*arena = i->arena;
	return picolPost(i, PICKLE_OK);
}

int pickle_new(pickle_t **i, allocator_fn a, void *arena) {
	picolCheck(i);
	picolCheck(a);
	*i = a(arena, NULL, 0, sizeof(**i));
	if (!*i)
		return PICKLE_ERROR;
	const int r = picolInitialize(*i, a, arena);
	return r == PICKLE_OK ? picolPost(*i, r) : PICKLE_ERROR;
}

int pickle_delete(pickle_t *i) {
	if (!i)
		return PICKLE_ERROR;
	/*picolCheck(i->initialized);*/
	const allocator_fn a = i->allocator;
	void *arena = i->arena;
	const int r = picolDeinitialize(i);
	a(arena, i, 0, 0);
	return r != PICKLE_OK ? r : PICKLE_OK;
}

int pickle_tests(allocator_fn fn, void *arena) {
	picolCheck(fn);
	if (!PICOL_DEFINE_TESTS)
		return PICKLE_OK;
	typedef int (*test_func)(allocator_fn fn, void *arena);
	static const test_func ts[] = {
		picolTestSmallString,
		picolTestUnescape,
		picolTestConvertNumber,
		picolTestConcat,
		picolTestEval,
		picolTestGetSetVar,
		picolTestParser,
		picolTestRegex,
	};
	int r = 0;
	for (size_t i = 0; i < (sizeof(ts) / sizeof(ts[0])); i++)
		if (ts[i](fn, arena) != 0)
			r = -1;
	return r != 0 ? PICKLE_ERROR : PICKLE_OK;
}

#if defined(PICKLE_DEFINE_MAIN)

#include <errno.h>
#include <stdlib.h>
#include <locale.h>
#include <time.h>

typedef struct { long allocs, frees, reallocs, total; uint64_t tick, after; } picol_heap_t;

/* NB. This allocator allows us to test failure modes of the interpreter */
static void *picolAllocator(void *arena, void *ptr, const size_t oldsz, const size_t newsz) {
	/* picolCheck(h && (h->frees <= h->allocs)); */
	picol_heap_t *h = arena;
	const int fail = h->after && h->tick++ > h->after;
	if (newsz == 0) { if (ptr) h->frees++; free(ptr); return fail ? arena : NULL; }
	if (newsz > oldsz) { h->reallocs += !!ptr; h->allocs++; h->total += newsz; return fail ? NULL : realloc(ptr, newsz); }
	return ptr;
}

static int picolRelease(pickle_t *i, void *ptr) {
	void *arena = NULL;
	allocator_fn fn = NULL;
	const int r1 = pickle_allocator_get(i, &fn, &arena);
	if (fn)
		fn(arena, ptr, 0, 0);
	return fn ? r1 : PICKLE_ERROR;
}

static void *picolReallocator(pickle_t *i, void *ptr, size_t sz) {
	void *arena = NULL;
	allocator_fn fn = NULL;
	if (pickle_allocator_get(i, &fn, &arena) != PICKLE_OK)
		abort();
	void *r = picolAllocator(arena, ptr, 0, sz);
	if (!r) {
		picolRelease(i, ptr);
		return NULL;
	}
	return r;
}

static int picolSlurp(pickle_t *i, FILE *input, size_t *length, const char *search_class, char **out) {
	picolCheck(i);
	picolCheck(input);
	picolCheck(out);
	picolCheck(*out == NULL);
	picolCheck(EOF == -1);
	char *m = NULL;
	const size_t bsz = search_class ? 80 : 16384;
	size_t sz = 0;
	if (length)
		*length = 0;
	*out = NULL;
	if (feof(input))
		return -1;
	if (ferror(input))
		return -2;
	for (;;) {
		if (ferror(input))
			return -2;
		if ((sz + bsz) < sz || (sz + bsz + 1) < sz) { /* file-error/overflow */
			(void)picolRelease(i, m);
			return -3;
		}
		if ((m = picolReallocator(i, m, sz + bsz + 1)) == NULL) /* picolReallocator frees on failure */
			return -4;
		if (search_class) { /* assume interactive, fetch character by character. */
			size_t j = 0;
			int ch = 0, done = 0;
			for (; j < bsz && ((ch = fgetc(input)) != EOF); ) {
				if (picolLocateChar(search_class, ch)) {
					done = 1;
					break;
				}
				m[sz + j++] = ch; /* move to before 'picolLocateChar' to include terminators */
			}
			sz += j;
			if (done || ch == EOF)
				break;
		} else { /* read until EOF, assume large file, fetch in one go */
			const size_t inc = fread(&m[sz], 1, bsz, input);
			sz += inc;
			if (feof(input))
				break;
		}
	}
	m[sz] = '\0'; /* ensure NUL termination */
	if (length)
		*length = sz;
	*out = m;
	return 0;
}

static int picolCommandGets(pickle_t *i, int argc, char **argv, void *pd) {
	if (argc != 1)
		return error(i, "Error command %s", argv[0]);
	size_t length = 0;
	char *line = NULL;
	const int sr = picolSlurp(i, (FILE*)pd, &length, "\n", &line);
	if (sr < EOF)
		return error(i, "Error slurp");
	if (sr == EOF && line == NULL)
		return ok(i, "EOF") == PICKLE_OK ? PICKLE_BREAK : PICKLE_ERROR;
	const int r = ok(i, "%s", line);
	return picolRelease(i, line) == PICKLE_OK ? r : PICKLE_ERROR;
}

static int picolCommandPuts(pickle_t *i, int argc, char **argv, void *pd) {
	FILE *out = pd;
	if (argc != 1 && argc != 2 && argc != 3)
		return error(i, "Error command %s", argv[0]);
	if (argc == 1)
		return fputc('\n', out) < 0 ? PICKLE_ERROR : PICKLE_OK;
	if (argc == 2)
		return fprintf(out, "%s\n", argv[1]) < 0 ? PICKLE_ERROR : PICKLE_OK;
	if (!picolCompare(argv[1], "-nonewline"))
		return fputs(argv[2], out) < 0 ? PICKLE_ERROR : PICKLE_OK;
	return error(i, "Error option %s", argv[1]);
}

static int picolCommandGetEnv(pickle_t *i, int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	if (argc != 2)
		return error(i, "Error command %s", argv[0]);
	const char *env = getenv(argv[1]);
	return ok(i, "%s", env ? env : "");
}

static int picolCommandExit(pickle_t *i, int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	if (argc != 2 && argc != 1)
		return error(i, "Error command %s", argv[0]);
	const char *code = argc == 2 ? argv[1] : "0";
	exit(atoi(code));
	return PICKLE_ERROR; /* unreachable */
}

static int picolConvert(pickle_t *i, const char *str, long *ld) {
	*ld = 0;
	if (sscanf(str, "%ld", ld) != 1)
		return error(i, "Error number %s", str);
	return PICKLE_OK;
}

static int picolCommandClock(pickle_t *i, const int argc, char **argv, void *pd) {
	PICOL_UNUSED(pd);
	time_t ts = 0;
	if (argc < 2)
		return error(i, "Error command %s", argv[0]);
	if (!picolCompare(argv[1], "clicks")) {
		const long t = (((double)(clock()) / (double)CLOCKS_PER_SEC) * 1000.0);
		return ok(i, "%ld", t);
	}
	if (!picolCompare(argv[1], "seconds"))
		return ok(i, "%ld", (long)time(&ts));
	if (!picolCompare(argv[1], "format")) {
		long tv = 0;
		if (argc != 3 && argc != 4)
			return error(i, "Error %s %s", argv[0], argv[1]);
		char buf[512] = { 0, };
		char *fmt = argc == 4 ? argv[3] : "%a %b %d %H:%M:%S %Z %Y";
		if (picolConvert(i, argv[2], &tv) < 0)
			return PICKLE_ERROR;
		ts = tv;
		struct tm *timeinfo = gmtime(&ts);
		const size_t sz = strftime(buf, sizeof buf, fmt, timeinfo);
		if (sz == 0 && fmt[0])
			return error(i, "Error %s %s", argv[1], fmt);
		return ok(i, "%s", buf);
	}
	return error(i, "Error %s %s", argv[0], argv[1]);
}

static int picolCommandSource(pickle_t *i, int argc, char **argv, void *pd) {
	if (argc != 1 && argc != 2)
		return error(i, "Error command %s", argv[0]);
	errno = 0;
	FILE *file = argc == 1 ? pd : fopen(argv[1], "rb");
	if (!file)
		return error(i, "Could not open file '%s' for reading: %s", argc == 1 ? "" : argv[1], strerror(errno));
	char *program = NULL;
	const int sr = picolSlurp(i, file, NULL, NULL, &program);
	if (file != pd)
		fclose(file);
	if (sr < EOF)
		return error(i, "Error slurp");
	const int r = pickle_eval(i, program);
	return picolRelease(i, program) == PICKLE_OK ? r : PICKLE_ERROR;
}

static int picolCommandHeap(pickle_t *i, int argc, char **argv, void *pd) {
	picol_heap_t *h = pd;
	if (argc != 2 && argc != 3)
		return error(i, "Error command %s", argv[0]);
	if (!picolCompare(argv[1], "frees"))         return ok(i, "%ld", h->frees);
	if (!picolCompare(argv[1], "allocations"))   return ok(i, "%ld", h->allocs);
	if (!picolCompare(argv[1], "total"))         return ok(i, "%ld", h->total);
	if (!picolCompare(argv[1], "reallocations")) return ok(i, "%ld", h->reallocs);
	if (argc != 3)
		return error(i, "Error command %s", argv[0]);
	if (!picolCompare(argv[1], "fail-after")) {
		long ld = 0;
		if (picolConvert(i, argv[2], &ld) < 0)
			return PICKLE_ERROR;
		h->after = ld;
		h->tick = 0;
		return PICKLE_OK;
	}
	return error(i, "Error %s %s", argv[0], argv[1]);
}

static int picolEvalFile(pickle_t *i, char *file) {
	const int r = file ?
		picolCommandSource(i, 2, (char*[2]){ "source", file }, NULL):
		picolCommandSource(i, 1, (char*[1]){ "source",      }, stdin);
	if (r != PICKLE_OK) {
		const char *f = NULL;
		if (pickle_result_get(i, &f) != PICKLE_OK)
			return r;
		if (fprintf(stdout, "%s\n", f) < 0)
			return PICKLE_ERROR;
	}
	return r;
}

static int picolSetArgv(pickle_t *i, int argc, char **argv) {
	const char *r = NULL;
	char **l = picolReallocator(i, NULL, (argc + 1) * sizeof (char*));
	if (!l)
		return PICKLE_ERROR;
	memcpy(&l[1], argv, argc * sizeof (char*));
	l[0] = "list";
	if (pickle_eval_args(i, argc + 1, l) != PICKLE_OK)
		goto fail;
	if (pickle_result_get(i, &r) != PICKLE_OK)
		goto fail;
	if (pickle_eval_args(i, 3, (char*[3]){"set", "argv", (char*)r}) != PICKLE_OK)
		goto fail;
	return picolRelease(i, l);
fail:
	(void)picolRelease(i, l);
	return PICKLE_ERROR;
}

int pickle_main(int argc, char **argv) {
	picol_heap_t h = { .allocs = 0, };
	pickle_t *i = NULL;
	if (!setlocale(LC_ALL, "C")) goto fail; /* NB. Locale sucks, it should be set to "C" at startup, but make sure. */
	if (pickle_tests(picolAllocator, &h)   != PICKLE_OK) goto fail;
	if (pickle_new(&i, picolAllocator, &h) != PICKLE_OK) goto fail;
	if (picolSetArgv(i, argc, argv)  != PICKLE_OK) goto fail;
	if (pickle_command_register(i, "gets",   picolCommandGets,   stdin)  != PICKLE_OK) goto fail;
	if (pickle_command_register(i, "puts",   picolCommandPuts,   stdout) != PICKLE_OK) goto fail;
	if (pickle_command_register(i, "getenv", picolCommandGetEnv, NULL)   != PICKLE_OK) goto fail;
	if (pickle_command_register(i, "exit",   picolCommandExit,   NULL)   != PICKLE_OK) goto fail;
	if (pickle_command_register(i, "source", picolCommandSource, NULL)   != PICKLE_OK) goto fail;
	if (pickle_command_register(i, "clock",  picolCommandClock,  NULL)   != PICKLE_OK) goto fail;
	if (pickle_command_register(i, "heap",   picolCommandHeap,   &h)     != PICKLE_OK) goto fail;
#ifdef PICKLE_EXTEND
	extern int pickle_extend(pickle_t *i);
	if (pickle_extend(i) != PICKLE_OK) goto fail;
#endif
	int r = 0;
	for (int j = 1; j < argc; j++) {
		r = picolEvalFile(i, argv[j]);
		if (r < 0)
			goto fail;
		if (r == PICKLE_BREAK)
			break;
	}
	if (argc == 1)
		r = picolEvalFile(i, NULL);
	return !!pickle_delete(i) || r < 0;
fail:
	(void)pickle_delete(i);
	return 1;
}
#endif /* PICKLE_DEFINE_MAIN */
#endif /* PICKLE_DEFINE_IMPLEMENTATION */
/* Clean up any macros */
#ifdef ok
#undef ok
#endif
#ifdef error
#undef error
#endif
#ifdef MIN
#undef MIN
#endif
#ifdef MAX
#undef MAX
#endif
