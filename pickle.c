/**@file pickle.c
 * @brief Pickle: A tiny TCL like interpreter
 *
 * A small TCL interpreter, called Pickle, that is basically just a copy
 * of the original written by Antirez, the original is available at
 *
 * <http://oldblog.antirez.com/post/picol.html>
 * <http://antirez.com/picol/picol.c.txt>
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
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Extensions/Changes by Richard James Howe, available at:
 * <https://github.com/howerj/pickle>
 * Also licensed under the same BSD license.
 *
 * NOTE: The string escaping could be improved upon. */

#include "pickle.h"
#include <assert.h>  /* !defined(NDEBUG): assert */
#include <ctype.h>   /* toupper, tolower, isalnum, isalpha, ... */
#include <stdint.h>  /* intptr_t */
#include <limits.h>  /* CHAR_BIT, LONG_MAX, LONG_MIN */
#include <stdarg.h>  /* va_list, va_start, va_end */
#include <stdio.h>   /* vsnprintf */
#include <stdlib.h>  /* !defined(DEFAULT_ALLOCATOR): free, malloc, realloc */
#include <string.h>  /* memset, memchr, strstr, strcmp, strncmp, strcpy, strlen, strchr */

#define PICKLE_MAX_RECURSION      (128) /* Recursion limit */

#define SMALL_RESULT_BUF_SZ       (96)
#define PRINT_NUMBER_BUF_SZ       (64 /* base 2 */ + 1 /* '-'/'+' */ + 1 /* NUL */)
#define VERSION                   (1989)
#define UNUSED(X)                 ((void)(X))
#define BUILD_BUG_ON(condition)   ((void)sizeof(char[1 - 2*!!(condition)]))
#define implies(P, Q)             assert(!(P) || (Q)) /* material implication, immaterial if NDEBUG defined */
#define mutual(P, Q)              (implies((P), (Q)), implies((Q), (P)))
#define verify(X)                 do { if (!(X)) { abort(); } } while (0)
#define member_size(TYPE, MEMBER) (sizeof(((TYPE *)0)->MEMBER)) /* apparently fine */
#define MIN(X, Y)                 ((X) > (Y) ? (Y) : (X))
#define MAX(X, Y)                 ((X) > (Y) ? (X) : (Y))

#ifndef PREPACK
#define PREPACK
#endif

#ifndef POSTPACK
#define POSTPACK
#endif

#ifdef NDEBUG
#define DEBUGGING         (0)
#else
#define DEBUGGING         (1)
#endif

#ifndef DEFINE_TESTS
#define DEFINE_TESTS      (1)
#endif

#ifndef DEFINE_MATHS
#define DEFINE_MATHS      (1)
#endif

#ifndef DEFINE_STRING
#define DEFINE_STRING     (1)
#endif

#ifndef DEFINE_LIST
#define DEFINE_LIST       (1)
#endif

#ifndef DEFINE_REGEX
#define DEFINE_REGEX      (1)
#endif

#ifndef DEFAULT_ALLOCATOR
#define DEFAULT_ALLOCATOR (1)
#endif

#ifndef USE_MAX_STRING
#define USE_MAX_STRING    (0)
#endif

#ifndef PICKLE_MAX_STRING
#define PICKLE_MAX_STRING (512) /* Max string/Data structure size, if USE_MAX_STRING != 0 */
#endif

#ifndef STRICT_NUMERIC_CONVERSION
#define STRICT_NUMERIC_CONVERSION (1)
#endif

#ifndef PICKLE_VERSION
#define PICKLE_VERSION (0x000000ul) /* all zeros = built incorrectly */
#endif

enum { PT_ESC, PT_STR, PT_CMD, PT_VAR, PT_SEP, PT_EOL, PT_EOF };

typedef struct {
	unsigned nocommands :1, /*< turn off commands */
		 noescape   :1, /*< turn off escape sequences */
		 novars     :1, /*< turn off variables */
		 noeval     :1; /*< turn off command evaluation */
} pickle_parser_opts_t; /*< options for parser/evaluator */

typedef PREPACK struct {
	const char *text;          /**< the program */
	const char *p;             /**< current text position */
	const char *start;         /**< token start */
	const char *end;           /**< token end */
	int *line;                 /**< pointer to line number */
	const char **ch;           /**< pointer to global test position */
	int len;                   /**< remaining length */
	int type;                  /**< token type, PT_... */
	pickle_parser_opts_t o;    /**< parser options */
	unsigned inside_quote: 1;   /**< true if inside " " */
} POSTPACK pickle_parser_t ;       /**< Parsing structure */

typedef PREPACK struct {
	char buf[SMALL_RESULT_BUF_SZ]; /**< small temporary buffer */
	char *p;                       /**< either points to buf or is allocated */
	size_t length;                 /**< length of 'p' */
} POSTPACK pickle_stack_or_heap_t;     /**< allocate on stack, or move to heap, depending on needs */

enum { PV_STRING, PV_SMALL_STRING, PV_LINK };

typedef union {
	char *ptr,  /**< pointer to string that has spilled over 'small' in size */
	     small[sizeof(char*)]; /**< string small enough to be stored in a pointer (including NUL terminator)*/
} compact_string_t; /**< either a pointer to a string, or a string stored in a pointer */

PREPACK struct pickle_var { /* strings are stored as either pointers, or as 'small' strings */
	compact_string_t name; /**< name of variable */
	union {
		compact_string_t val;    /**< value */
		struct pickle_var *link; /**< link to another variable */
	} data;
	struct pickle_var *next; /**< next variable in list of variables */

	unsigned type      : 2; /* type of data; string (pointer/small), or link (NB. Could add number type) */
	unsigned smallname : 1; /* if true, name is stored as small string */
} POSTPACK;

PREPACK struct pickle_command {
	char *name;                  /**< name of function */
	pickle_command_func_t func;  /**< pointer to function that implements this command */
	struct pickle_command *next; /**< next command in list (chained hash table) */
	void *privdata;              /**< (optional) private data for function */
} POSTPACK;

PREPACK struct pickle_call_frame {        /**< A call frame, organized as a linked list */
	struct pickle_var *vars;          /**< first variable in linked list of variables */
	struct pickle_call_frame *parent; /**< parent is NULL at top level */
} POSTPACK;

PREPACK struct pickle_interpreter { /**< The Pickle Interpreter! */
	char result_buf[SMALL_RESULT_BUF_SZ];/**< store small results here without allocating */
	pickle_allocator_t allocator;        /**< custom allocator, if desired */
	const char *result;                  /**< result of an evaluation */
	const char *ch;                      /**< the current text position; set if line != 0 */
	struct pickle_call_frame *callframe; /**< call stack */
	struct pickle_command **table;       /**< hash table */
	long length;                         /**< buckets in hash table */
	int level;                           /**< level of nesting */
	int line;                            /**< current line number */
	unsigned initialized    :1;          /**< if true, interpreter is initialized and ready to use */
	unsigned static_result  :1;          /**< internal use only: if true, result should not be freed */
	unsigned inside_uplevel :1;          /**< true if executing inside an uplevel command */
	unsigned inside_unknown :1;          /**< true if executing inside the 'unknown' proc */
} POSTPACK;

typedef PREPACK struct {
	const char *start,  /**< start of match, NULL if no match */
	      *end;         /**< end of match, NULL if no match */
	int max;            /**< */
	unsigned type   :2, /**< select regex type; lazy, greedy or possessive */
		 nocase :1; /**< ignore case when matching */
} POSTPACK pickle_regex_t; /**< */

typedef struct PREPACK { int argc; char **argv; } POSTPACK args_t;

typedef struct pickle_var pickle_var_t;
typedef struct pickle_call_frame pickle_call_frame_t;
typedef struct pickle_command pickle_command_t;

typedef long number_t;
typedef unsigned long unumber_t;
#define NUMBER_MIN (LONG_MIN)
#define NUMBER_MAX (LONG_MAX)

static const char  string_empty[]     = "";              /* Space saving measure */
static const char  string_oom[]       = "Out Of Memory"; /* Cannot allocate this, obviously */
static const char *string_white_space = " \t\n\r\v";
static const char *string_digits      = "0123456789abcdefghijklmnopqrstuvwxyz";

static inline void static_assertions(void) { /* A neat place to put these */
	BUILD_BUG_ON(PICKLE_MAX_STRING    < 128);
	BUILD_BUG_ON(sizeof (struct pickle_interpreter) > PICKLE_MAX_STRING);
	BUILD_BUG_ON(sizeof (string_oom) > SMALL_RESULT_BUF_SZ);
	BUILD_BUG_ON(PICKLE_MAX_RECURSION < 8);
	BUILD_BUG_ON(PICKLE_OK    !=  0);
	BUILD_BUG_ON(PICKLE_ERROR != -1);
}

static inline int compare(const char *a, const char *b) {
	assert(a);
	assert(b);
	if (USE_MAX_STRING)
		return strncmp(a, b, PICKLE_MAX_STRING);
	return strcmp(a, b);
}

static inline size_t picolStrnlen(const char *s, size_t length) {
	assert(s);
	size_t r = 0;
	for (r = 0; r < length && *s; s++, r++)
		;
	return r;
}

static inline size_t picolStrlen(const char *s) {
	assert(s);
	return USE_MAX_STRING ? picolStrnlen(s, PICKLE_MAX_STRING) : strlen(s);
}

static inline void *move(void *dst, const void *src, const size_t length) {
	assert(dst);
	assert(src);
	return memmove(dst, src, length);
}

static inline void *zero(void *m, const size_t length) {
	assert(m);
	return memset(m, 0, length);
}

static inline char *copy(char *dst, const char *src) {
	assert(src);
	assert(dst);
	return strcpy(dst, src);
}

static inline char *find(const char *haystack, const char *needle) {
	assert(haystack);
	assert(needle);
	return strstr(haystack, needle);
}

static inline char *locateChar(const char *s, const int c) {
	assert(s);
	return strchr(s, c);
}

static inline void *locateByte(const void *m, int c, size_t n) {
	assert(m);
	return memchr(m, c, n);
}

static int picolForceResult(pickle_t *i, const char *result, const int is_static);

static inline void *picolMalloc(pickle_t *i, size_t size) {
	assert(i);
	assert(size > 0); /* we should not allocate any zero length objects here */
	if (USE_MAX_STRING && size > PICKLE_MAX_STRING)
		return NULL;
	void *r = i->allocator.malloc(i->allocator.arena, size);
	if (!r && size)
		(void)picolForceResult(i, string_oom, 1); /* does not allocate, may free */
	return r;
}

static inline void *picolRealloc(pickle_t *i, void *p, size_t size) {
	assert(i);
	if (USE_MAX_STRING && size > PICKLE_MAX_STRING)
		return NULL;
	void *r = i->allocator.realloc(i->allocator.arena, p, size);
	if (!r && size)
		(void)picolForceResult(i, string_oom, 1); /* does not allocate, may free */
	return r;
}

static inline int picolFree(pickle_t *i, void *p) {
	assert(i);
	assert(i->allocator.free);
	const int r = i->allocator.free(i->allocator.arena, p);
	assert(r == 0); /* should just return it, but we do not check it throughout program */
	if (r != 0) {
		static const char msg[] = "Invalid free";
		BUILD_BUG_ON(sizeof(msg) > SMALL_RESULT_BUF_SZ);
		pickle_set_result_string(i, msg);
	}
	return r;
}

static inline int picolIsBaseValid(const int base) {
	return base >= 2 && base <= 36; /* Base '0' is not a special case */
}

static inline int picolDigit(const int digit) {
	const char *found = locateChar(string_digits, tolower(digit));
	return found ? (int)(found - string_digits) : -1;
}

static inline int picolIsDigit(const int digit, const int base) {
	assert(picolIsBaseValid(base));
	const int r = picolDigit(digit);
	return r < base ? r : -1;
}

static int picolConvertBaseNNumber(pickle_t *i, const char *s, number_t *out, int base) {
	assert(i);
	assert(i->initialized);
	assert(s);
	assert(picolIsBaseValid(base));
	static const size_t max = MIN(PRINT_NUMBER_BUF_SZ, PICKLE_MAX_STRING);
	number_t result = 0;
	int ch = s[0];
	const int negate = ch == '-';
	const int prefix = negate || s[0] == '+';
	*out = 0;
	if (STRICT_NUMERIC_CONVERSION && prefix && !s[prefix])
		return pickle_set_result_error(i, "Invalid number %s", s);
	if (STRICT_NUMERIC_CONVERSION && !ch)
		return pickle_set_result_error(i, "Invalid number %s", s);
	for (size_t j = prefix; j < max && (ch = s[j]); j++) {
		const number_t digit = picolIsDigit(ch, base);
		if (digit < 0)
			break;
		result = digit + (result * (number_t)base);
	}
	if (STRICT_NUMERIC_CONVERSION && ch)
		return pickle_set_result_error(i, "Invalid number %s", s);
	*out = negate ? -result : result;
	return PICKLE_OK;
}

static int picolStringToNumber(pickle_t *i, const char *s, number_t *out) {
	assert(i);
	assert(s);
	assert(out);
	return picolConvertBaseNNumber(i, s, out, 10);
}

static inline int picolCompareCaseInsensitive(const char *a, const char *b) {
	assert(a);
	assert(b);
	for (size_t i = 0; ; i++) {
		const int ach = tolower(a[i]);
		const int bch = tolower(b[i]);
		const int diff = ach - bch;
		if (!ach || diff)
			return diff;
	}
	return 0;
}

static inline int picolLogarithm(number_t a, const number_t b, number_t *c) {
	assert(c);
	number_t r = -1;
	*c = r;
	if (a <= 0 || b < 2)
		return PICKLE_ERROR;
	do r++; while (a /= b);
	*c = r;
	return PICKLE_OK;
}

static int picolPower(number_t base, number_t exp, number_t *r) {
	assert(r);
	number_t result = 1, negative = 1;
	*r = 0;
	if (exp < 0)
		return PICKLE_ERROR;
	if (base < 0) {
		base = -base;
		negative = -1;
	}
	for (;;) {
		if (exp & 1)
			result *= base;
		exp /= 2;
		if (!exp)
			break;
		base *= base;
	}
	*r = result * negative;
	return PICKLE_OK;
}

/* This is may seem like an odd function, say for small allocation we want to
 * keep them on the stack, but move them to the heap when they get too big, we can use
 * the 'picolStackOrHeapAlloc'/'picolStackOrHeapFree' functions to manage this. */
static int picolStackOrHeapAlloc(pickle_t *i, pickle_stack_or_heap_t *s, size_t needed) {
	assert(i);
	assert(s);

	if (s->p == NULL) { /* take care of initialization */
		s->p = s->buf;
		s->length = sizeof (s->buf);
	}

	if (needed <= s->length)
		return PICKLE_OK;
	if (USE_MAX_STRING && needed > PICKLE_MAX_STRING)
		return PICKLE_ERROR;
	if (s->p == s->buf) {
		if (!(s->p = picolMalloc(i, needed)))
			return PICKLE_ERROR;
		s->length = needed;
		return PICKLE_OK;
	}
	void *new = picolRealloc(i, s->p, needed);
	if (!new) {
		(void)picolFree(i, s->p);
		s->p = NULL;
		s->length = 0;
		return PICKLE_ERROR;
	}
	s->p = new;
	s->length = needed;
	return PICKLE_OK;
}

static int picolStackOrHeapFree(pickle_t *i, pickle_stack_or_heap_t *s) {
	assert(i);
	assert(s);
	if (s->p != s->buf) {
		const int r = picolFree(i, s->p);
		s->p = NULL; /* prevent double free */
		return r;
	}
	return PICKLE_OK; /* pointer == buffer, no need to free */
}

static int picolOnHeap(pickle_t *i, pickle_stack_or_heap_t *s) {
	assert(i); UNUSED(i);
	assert(s);
	return s->p && s->p != s->buf;
}

static char *picolStrdup(pickle_t *i, const char *s) {
	assert(i);
	assert(s);
	const size_t l = picolStrlen(s);
	char *r = picolMalloc(i, l + 1);
	return r ? move(r, s, l + 1) : r;
}

static inline unsigned long picolHashString(const char *s) { /* DJB2 Hash, <http://www.cse.yorku.ca/~oz/hash.html> */
	assert(s);
	unsigned long h = 5381, ch = 0;
	for (size_t i = 0; (ch = s[i]); i++) {
		implies(USE_MAX_STRING, i < PICKLE_MAX_STRING);
		h = ((h << 5) + h) + ch;
	}
	return h;
}

static inline pickle_command_t *picolGetCommand(pickle_t *i, const char *s) {
	assert(s);
	assert(i);
	pickle_command_t *np = NULL;
	for (np = i->table[picolHashString(s) % i->length]; np != NULL; np = np->next)
		if (!compare(s, np->name))
			return np; /* found */
	return NULL; /* not found */
}

static int picolFreeResult(pickle_t *i) {
	assert(i);
	return i->static_result ? PICKLE_OK : picolFree(i, (char*)i->result);
}

static int picolForceResult(pickle_t *i, const char *result, const int is_static) {
	assert(i);
	assert(result);
	int r = picolFreeResult(i);
	i->static_result = is_static;
	i->result = result;
	return r;
}

static int picolCommandCallProc(pickle_t *i, const int argc, char **argv, void *pd);
static int picolCommandCallVariadic(pickle_t *i, const int argc, char **argv, void *pd);

static int picolIsDefinedProc(pickle_command_func_t func) {
	return func == picolCommandCallProc || func == picolCommandCallVariadic;
}

/* <https://stackoverflow.com/questions/4384359/> */
static int picolRegisterCommand(pickle_t *i, const char *name, pickle_command_func_t func, void *privdata) {
	assert(i);
	assert(name);
	assert(func);
	pickle_command_t *np = picolGetCommand(i, name);
	if (np) {
		if (picolIsDefinedProc(func)) {
			char **procdata = privdata;
			(void)picolFree(i, procdata[0]);
			(void)picolFree(i, procdata[1]);
			(void)picolFree(i, procdata);
		}
		return pickle_set_result_error(i, "Invalid redefinition %s", name);
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
	assert(i);
	assert(name);
	pickle_command_t **p = &i->table[picolHashString(name) % i->length];
	pickle_command_t *c = *p;
	for (; c; c = c->next) {
		if (!compare(c->name, name)) {
			*p = c->next;
			return picolFreeCmd(i, c);
		}
		p = &c->next;
	}
	return pickle_set_result_error(i, "Invalid variable %s", name);;
}

static int advance(pickle_parser_t *p) {
	assert(p);
	if (p->len <= 0)
		return PICKLE_ERROR;
	if (p->len && !(*p->p))
		return PICKLE_ERROR;
	if (p->line && *p->line/*NULL disables line count*/ && *p->ch < p->p) {
		*p->ch = p->p;
		if (*p->p == '\n')
			(*p->line)++;
	}
	p->p++;
	p->len--;
	if (p->len && !(*p->p))
		return PICKLE_ERROR;
	return PICKLE_OK;
}

static inline void picolParserInitialize(pickle_parser_t *p, pickle_parser_opts_t *o, const char *text, int *line, const char **ch) {
	/* NB. assert(o || !o); */
	assert(p);
	assert(text);
	zero(p, sizeof *p);
	p->text = text;
	p->p    = text;
	p->len  = strlen(text); /* unbounded! */
	p->type = PT_EOL;
	p->line = line;
	p->ch   = ch;
	p->o    = o ? *o : p->o;
}

static inline int picolIsSpaceChar(const int ch) {
	return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static inline int picolParseSep(pickle_parser_t *p) {
	assert(p);
	p->start = p->p;
	while (picolIsSpaceChar(*p->p))
		if (advance(p) != PICKLE_OK)
			return PICKLE_ERROR;
	p->end  = p->p - 1;
	p->type = PT_SEP;
	return PICKLE_OK;
}

static inline int picolParseEol(pickle_parser_t *p) {
	assert(p);
	p->start = p->p;
	while (picolIsSpaceChar(*p->p) || *p->p == ';')
		if (advance(p) != PICKLE_OK)
			return PICKLE_ERROR;
	p->end  = p->p - 1;
	p->type = PT_EOL;
	return PICKLE_OK;
}

static inline int picolParseCommand(pickle_parser_t *p) {
	assert(p);
	if (advance(p) != PICKLE_OK)
		return PICKLE_ERROR;
	p->start = p->p;
	for (int level = 1, blevel = 0; p->len;) {
		if (*p->p == '[' && blevel == 0) {
			level++;
		} else if (*p->p == ']' && blevel == 0) {
			if (!--level)
				break;
		} else if (*p->p == '\\') {
			if (advance(p) != PICKLE_OK)
				return PICKLE_ERROR;
		} else if (*p->p == '{') {
			blevel++;
		} else if (*p->p == '}') {
			if (blevel != 0)
				blevel--;
		}
		if (advance(p) != PICKLE_OK)
			return PICKLE_ERROR;
	}
	if (*p->p != ']')
		return PICKLE_ERROR;
	p->end  = p->p - 1;
	p->type = PT_CMD;
	if (advance(p) != PICKLE_OK)
		return PICKLE_ERROR;
	return PICKLE_OK;
}

static inline int picolIsVarChar(const int ch) {
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
}

static inline int picolParseVar(pickle_parser_t *p) {
	assert(p);
	if (advance(p) != PICKLE_OK) /* skip the $ */
		return PICKLE_ERROR;
	p->start = p->p;
	for (;picolIsVarChar(*p->p);)
	       	if (advance(p) != PICKLE_OK)
			return PICKLE_ERROR;
	if (p->start == p->p) { /* It's just a single char string "$" */
		p->start = p->p - 1;
		p->end   = p->p - 1;
		p->type  = PT_STR;
	} else {
		p->end  = p->p - 1;
		p->type = PT_VAR;
	}
	return PICKLE_OK;
}

static inline int picolParseBrace(pickle_parser_t *p) {
	assert(p);
	if (advance(p) != PICKLE_OK)
		return PICKLE_ERROR;
	p->start = p->p;
	for (int level = 1;;) {
		if (p->len >= 2 && *p->p == '\\') {
			if (advance(p) != PICKLE_OK)
				return PICKLE_ERROR;
		} else if (p->len == 0 || *p->p == '}') {
			level--;
			if (level == 0 || p->len == 0) {
				p->end  = p->p - 1;
				p->type = PT_STR;
				if (p->len)
					return advance(p); /* Skip final closed brace */
				return PICKLE_OK;
			}
		} else if (*p->p == '{') {
			level++;
		}
		if (advance(p) != PICKLE_OK)
			return PICKLE_ERROR;
	}
	return PICKLE_OK; /* unreached */
}

static int picolParseString(pickle_parser_t *p) {
	assert(p);
	const int newword = (p->type == PT_SEP || p->type == PT_EOL || p->type == PT_STR);
	if (newword && *p->p == '{') {
		return picolParseBrace(p);
	} else if (newword && *p->p == '"') {
		p->inside_quote = 1;
		if (advance(p) != PICKLE_OK)
			return PICKLE_ERROR;
	}
	p->start = p->p;
	for (;p->len;) {
		switch (*p->p) {
		case '\\':
			if (p->o.noescape)
				break;
			if (p->len >= 2)
				if (advance(p) != PICKLE_OK)
					return PICKLE_ERROR;
			break;
		case '$':
			if (p->o.novars)
				break;
			p->end  = p->p - 1;
			p->type = PT_ESC;
			return PICKLE_OK;
		case '[':
			if (p->o.nocommands)
				break;
			p->end  = p->p - 1;
			p->type = PT_ESC;
			return PICKLE_OK;
		case '\n': case ' ': case '\t': case '\r': case ';':
			if (!p->inside_quote) {
				p->end  = p->p - 1;
				p->type = PT_ESC;
				return PICKLE_OK;
			}
			break;
		case '"':
			if (p->inside_quote) {
				p->end  = p->p - 1;
				p->type = PT_ESC;
				p->inside_quote = 0;
				return advance(p);
			}
			break;
		}
		if (advance(p) != PICKLE_OK)
			return PICKLE_ERROR;
	}
	p->end = p->p - 1;
	p->type = PT_ESC;
	return PICKLE_OK;
}

static inline int picolParseComment(pickle_parser_t *p) {
	assert(p);
	while (p->len && *p->p != '\n')
		if (advance(p) != PICKLE_OK)
			return PICKLE_ERROR;
	return PICKLE_OK;
}

static int picolGetToken(pickle_parser_t *p) {
	assert(p);
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
			if (r == PICKLE_OK && p->o.nocommands && p->type == PT_CMD) {
				p->start--, p->end++;
				p->type = PT_STR;
				assert(*p->start == '[' && *p->end == ']');
			}
			return r;
		}
		case '$':
			return p->o.novars ? picolParseString(p) : picolParseVar(p);
		case '#':
			if (p->type == PT_EOL) {
				if (picolParseComment(p) != PICKLE_OK)
					return PICKLE_ERROR;
				continue;
			}
			return picolParseString(p);
		default:
			return picolParseString(p);
		}
	}
	if (p->type != PT_EOL && p->type != PT_EOF)
		p->type = PT_EOL;
	else
		p->type = PT_EOF;
	return PICKLE_OK;
}

static pickle_var_t *picolGetVar(pickle_t *i, const char *name, int link) {
	assert(i);
	assert(name);
	pickle_var_t *v = i->callframe->vars;
	while (v) {
		const char *n = v->smallname ? &v->name.small[0] : v->name.ptr;
		assert(n);
		if (!compare(n, name)) {
			if (link)
				while (v->type == PV_LINK) { /* NB. Could resolve link at creation? */
					assert(v != v->data.link); /* Cycle? */
					v = v->data.link;
				}
			implies(v->type == PV_STRING, v->data.val.ptr);
			return v;
		}
		/* See <https://en.wikipedia.org/wiki/Cycle_detection>,
		 * <https://stackoverflow.com/questions/2663115>, or "Floyd's
		 * cycle-finding algorithm" for proper loop detection */
		assert(v != v->next); /* Cycle? */
		v = v->next;
	}
	return NULL;
}

static int picolFreeVarName(pickle_t *i, pickle_var_t *v) {
	assert(i);
	assert(v);
	return v->smallname ? PICKLE_OK : picolFree(i, v->name.ptr);
}

static int picolFreeVarVal(pickle_t *i, pickle_var_t *v) {
	assert(i);
	assert(v);
	return v->type == PV_STRING ? picolFree(i, v->data.val.ptr) : PICKLE_OK;
}

/* return: non-zero if and only if val fits in a small string */
static inline int picolIsSmallString(const char *val) {
	assert(val);
	return !!locateByte(val, 0, member_size(compact_string_t, small));
}

static int picolSetVarString(pickle_t *i, pickle_var_t *v, const char *val) {
	assert(i);
	assert(v);
	assert(val);
	if (picolIsSmallString(val)) {
		v->type = PV_SMALL_STRING;
		copy(v->data.val.small, val);
		return PICKLE_OK;
	}
	v->type = PV_STRING;
	return (v->data.val.ptr = picolStrdup(i, val)) ? PICKLE_OK : PICKLE_ERROR;
}

static inline int picolSetVarName(pickle_t *i, pickle_var_t *v, const char *name) {
	assert(i);
	assert(v);
	assert(name);
	if (picolIsSmallString(name)) {
		v->smallname = 1;
		copy(v->name.small, name);
		return PICKLE_OK;
	}
	v->smallname = 0;
	return (v->name.ptr = picolStrdup(i, name)) ? PICKLE_OK : PICKLE_ERROR;
}

static const char *picolGetVarVal(pickle_var_t *v) {
	assert(v);
	assert((v->type == PV_SMALL_STRING) || (v->type == PV_STRING));
	switch (v->type) {
	case PV_SMALL_STRING: return v->data.val.small;
	case PV_STRING:       return v->data.val.ptr;
	}
	return NULL;
}

static inline void picolSwapString(char **a, char **b) {
	assert(a);
	assert(b);
	char *t = *a;
	*a = *b;
	*b = t;
}

static inline void picolSwapChar(char * const a, char * const b) {
	assert(a);
	assert(b);
	const char t = *a;
	*a = *b;
	*b = t;
}

static inline char *reverse(char *s, size_t length) { /* Modifies Argument */
	assert(s);
	for (size_t i = 0; i < (length/2); i++)
		picolSwapChar(&s[i], &s[(length - i) - 1]);
	return s;
}

static int picolNumberToString(char buf[/*static*/ 64/*base 2*/ + 1/*'+'/'-'*/ + 1/*NUL*/], number_t in, int base) {
	assert(buf);
	int negate = 0;
	size_t i = 0;
	unumber_t dv = in;
	if (!picolIsBaseValid(base))
		return PICKLE_ERROR;
	if (in < 0) {
		dv = -in;
		negate = 1;
	}
	do
		buf[i++] = string_digits[dv % base];
	while ((dv /= base));
	if (negate)
		buf[i++] = '-';
	buf[i] = 0;
	reverse(buf, i);
	return PICKLE_OK;
}

static char *picolVsprintf(pickle_t *i, const char *fmt, va_list ap) {
	assert(i);
	assert(fmt);
	pickle_stack_or_heap_t h = { .p = NULL };
	BUILD_BUG_ON(sizeof (h.buf) != SMALL_RESULT_BUF_SZ);
	int needed = SMALL_RESULT_BUF_SZ;
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
		if (picolStackOrHeapAlloc(i, &h, r + 1) != PICKLE_OK)
			goto fail;
	}
	if (!picolOnHeap(i, &h)) {
		char *r = picolStrdup(i, h.buf);
		picolStackOrHeapFree(i, &h);
		return r;
	}
	return h.p;
fail:
	picolStackOrHeapFree(i, &h);
	return NULL;
}

static char *picolAStrCat(pickle_t *i, const char *a, const char *b) {
	assert(i);
	assert(a);
	assert(b);
	const size_t al = picolStrlen(a), bl = picolStrlen(b);
	const size_t nl = al + bl + 1;
	if (nl < al || nl < bl) /* overflow */
		return NULL;
	char *r = picolMalloc(i, nl);
	if (!r)
		return NULL;
	move(r, a, al);
	move(r + al, b, bl);
	r[al + bl] = '\0';
	return r;
}

static int picolSetResultNumber(pickle_t *i, const number_t result) {
	assert(i);
	char buffy/*<3*/[PRINT_NUMBER_BUF_SZ] = { 0 };
	if (picolNumberToString(buffy, result, 10) != PICKLE_OK)
		return pickle_set_result_error(i, "Invalid conversion");
	return pickle_set_result_string(i, buffy);
}

static int picolSetVarInteger(pickle_t *i, const char *name, const number_t r) {
	assert(i);
	assert(name);
	char buffy[PRINT_NUMBER_BUF_SZ] = { 0 };
	if (picolNumberToString(buffy, r, 10) != PICKLE_OK)
		return pickle_set_result_error(i, "Invalid conversion");
	return pickle_set_var_string(i, name, buffy);
}

static inline void picolAssertCommandPreConditions(pickle_t *i, const int argc, char **argv, void *pd) {
	/* UNUSED is used to suppress warnings if NDEBUG is defined */
	UNUSED(i);    assert(i);
	UNUSED(argc); assert(argc >= 1);
	UNUSED(argv); assert(argv);
	UNUSED(pd);   /* pd may be NULL*/
	if (DEBUGGING)
		for (int j = 0; j < argc; j++)
			assert(argv[j]);
}

static inline void picolAssertCommandPostConditions(pickle_t *i, const int retcode) {
	UNUSED(i);  assert(i);
	assert(i->initialized);
	assert(i->result);
	assert(i->level >= 0);
	UNUSED(retcode); /* arbitrary returns codes allowed, otherwise assert((retcode >= 0) && (retcode < PICKLE_LAST_ENUM)); */
}

static int picolFreeArgList(pickle_t *i, const int argc, char **argv) {
	assert(i);
	assert(argc >= 0);
	implies(argc != 0, argv);
	int r = 0;
	for (int j = 0; j < argc; j++)
		if (picolFree(i, argv[j]) != PICKLE_OK)
			r = -1;
	if (picolFree(i, argv) != PICKLE_OK)
		r = -1;
	return r;
}

static int hexCharToNibble(int c) {
	c = tolower(c);
	if ('a' <= c && c <= 'f')
		return 0xa + c - 'a';
	return c - '0';
}

/* converts up to two characters and returns number of characters converted */
static int hexStr2ToInt(const char *str, int *const val) {
	assert(str);
	assert(val);
	*val = 0;
	if (!isxdigit(*str))
		return 0;
	*val = hexCharToNibble(*str++);
	if (!isxdigit(*str))
		return 1;
	*val = (*val << 4) + hexCharToNibble(*str);
	return 2;
}

static int picolUnEscape(char *r, size_t length) {
	assert(r);
	if (!length)
		return -1;
	size_t k = 0;
	for (size_t j = 0, ch = 0; (ch = r[j]) && k < length; j++, k++) {
		if (ch == '\\') {
			j++;
			switch (r[j]) {
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
				const int pos = hexStr2ToInt(&r[j + 1], &val);
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
	assert(i);
	assert(s);
	char *m = picolMalloc(i, length + 2 + 1/*NUL*/);
	if (!m)
		return m;
	m[0] = '{';
	if (length)
		move(m + 1, s, length);
	m[length + 1] = '}';
	m[length + 2] = '\0';
	return m;
}

static int picolStringNeedsEscaping(const char *s) {
	assert(s);
	long braces = 0;
	char start = s[0], end = 0, ch = 0, sp = 0;
	for (size_t i = 0; (ch = s[i]); s++) {
		end = ch;
		if (locateChar(string_white_space, ch))
			sp = 1;
		if (ch == '{') braces++;
		if (ch == '}') braces--;
		if (ch == '\\') {
			ch = s[++i];
			if (!ch)
				return 1;
		}
	}
	if (!start || sp)
		return braces || !(start == '{' && end == '}');
	return 0;
}

static const char *trimleft(const char *class, const char *s) { /* Returns pointer to s */
	assert(class);
	assert(s);
	size_t j = 0, k = 0;
	while (s[j] && locateChar(class, s[j++]))
		k = j;
	return &s[k];
}

static const char *lastUnmatching(const char *class, const char *s) {
	assert(class);
	assert(s);
	const size_t length = picolStrlen(s);
	size_t j = length - 1;
	if (j > length)
		return &s[0];
	while (j > 0 && locateChar(class, s[j]))
		j--;
	return &s[j];
}

static void trimright(const char *class, char *s) { /* Modifies argument */
	assert(class);
	assert(s);
	const size_t j = lastUnmatching(class, s) - s;
	if (s[j])
		s[j + !locateChar(class, s[j])] = '\0';
}

static char *trim(const char *class, char *s) { /* Modifies argument */
	assert(class);
	assert(s);
	char *r = (char*)trimleft(class, s);
	trimright(class, r);
	return r;
}

static char *concatenate(pickle_t *i, const char *join, const int argc, char **argv, const int doEscape, const int trim) {
	assert(i);
	assert(join);
	assert(argc >= 0);
	implies(argc > 0, argv != NULL);
	pickle_stack_or_heap_t h = { .p = NULL };
	if (argc == 0)
		return picolStrdup(i, "");
	const size_t jl = picolStrlen(join);
	size_t l = 0;
	char   *esc = picolMalloc(i, argc * sizeof *esc);
	size_t  *ls = picolMalloc(i, argc * sizeof *ls);
	char *str = NULL;
	int args = 0;
	if (!esc || !ls)
		goto end;
	zero(esc, argc * sizeof *esc);
	for (int j = 0; j < argc; j++) {
		if (!argv[j])
			continue;
		args++;
		const size_t sz = picolStrlen(argv[j]);
		if (doEscape)
			esc[j] = picolStringNeedsEscaping(argv[j]);
		ls[j] = sz;
		l += sz + jl;
	}
	if (USE_MAX_STRING && ((l + 1) >= PICKLE_MAX_STRING))
		goto end;
	if (picolStackOrHeapAlloc(i, &h, l) != PICKLE_OK)
		goto end;
	l = 0;
	for (int j = 0, k = 0; j < argc; j++) {
		char *arg = argv[j];
		if (!arg)
			continue;

		if (trim) {
			char *lt = (char*)trimleft(string_white_space, arg);
			char *rt = (char*)lastUnmatching(string_white_space, lt);
			ls[j] = 1 + rt - lt;
			arg = lt;
		}

		k++;
		const int escape = esc[j];
		char *f = escape ? picolEscape(i, arg, ls[j]) : NULL;
		char *p = escape ? f : arg;
		if (!p)
			goto end;
		implies(USE_MAX_STRING, l < PICKLE_MAX_STRING);
		move(h.p + l, p, ls[j] + (2 * escape));
		l += ls[j] + (2 * escape);
		if (jl && k < args) {
			implies(USE_MAX_STRING, l < PICKLE_MAX_STRING);
			move(h.p + l, join, jl);
			l += jl;
		}
		if (picolFree(i, f) != PICKLE_OK)
			goto end;
	}
	h.p[l] = '\0';
	if (picolOnHeap(i, &h))
		return h.p;
	str = picolStrdup(i, h.p);
end:
	(void)picolFree(i, ls);
	(void)picolFree(i, esc);
	(void)picolStackOrHeapFree(i, &h);
	return str;
}

static args_t picolArgs(pickle_t *i, pickle_parser_opts_t *o, const char *s) {
	assert(i);
	assert(s);
	assert(o);
	args_t r = { .argc = 0, .argv = NULL };
	pickle_parser_t p = { .p = NULL };
	picolParserInitialize(&p, o, s, NULL, NULL);
	for (;;) {
		if (picolGetToken(&p) != PICKLE_OK)
			goto err;
		if (p.type == PT_EOF)
			break;
		if (p.type == PT_STR || p.type == PT_VAR || p.type == PT_CMD || p.type == PT_ESC) {
			const size_t tl = (p.end - p.start) + 1;
			char *t = picolMalloc(i, tl + 1);
			char **old = r.argv;
			if (!t)
				goto err;
			move(t, p.start, tl);
			t[tl] = '\0';
			if (!(r.argv = picolRealloc(i, r.argv, sizeof(char*)*(r.argc + 1)))) {
				r.argv = old;
				goto err;
			}
			r.argv[r.argc++] = t;
		}
	}
	if (!r.argv) {
		assert(r.argc == 0);
		r.argv = picolMalloc(i, sizeof (char *));
	}
	return r;
err:
	(void)picolFreeArgList(i, r.argc, r.argv);
	return (args_t){ 0, NULL };
}

static char **picolArgsGrow(pickle_t *i, int *argc, char **argv) {
	assert(i);
	assert(argc);
	assert(argv);
	const int n = *argc + 1;
	assert(n > 0);
	char **old = argv;
	if (!(argv = picolRealloc(i, argv, sizeof(char*) * n))) {
		(void)picolFreeArgList(i, *argc, old);
		return NULL;
	}
	*argc = n;
	argv[n - 1] = NULL;
	return argv;
}

static inline int picolDoCommand(pickle_t *i, int argc, char *argv[]) {
	assert(i);
	assert(argc >= 1);
	assert(argv);
	if (pickle_set_result_empty(i) != PICKLE_OK)
		return PICKLE_ERROR;
	pickle_command_t *c = picolGetCommand(i, argv[0]);
	if (c == NULL) {
		if (i->inside_unknown || ((c = picolGetCommand(i, "unknown")) == NULL))
			return pickle_set_result_error(i, "Invalid command %s", argv[0]);
		char *arg2 = concatenate(i, " ", argc, argv, 1, 0);
		if (!arg2)
			return PICKLE_ERROR;
		char *nargv[] = { "unknown", arg2 };
		i->inside_unknown = 1;
		picolAssertCommandPreConditions(i, 2, nargv, c->privdata);
		const int r = c->func(i, 2, nargv, c->privdata);
		picolAssertCommandPostConditions(i, r);
		i->inside_unknown = 0;
		if (picolFree(i, arg2) != PICKLE_OK)
			return PICKLE_ERROR;
		return r;
	}
	picolAssertCommandPreConditions(i, argc, argv, c->privdata);
	const int r = c->func(i, argc, argv, c->privdata);
	picolAssertCommandPostConditions(i, r);
	return r;
}

static int picolEvalAndSubst(pickle_t *i, pickle_parser_opts_t *o, const char *eval) {
	assert(i);
	assert(i->initialized);
	/* NB: assert(o || !o); */
	assert(eval);
	pickle_parser_t p = { .p = NULL };
	int retcode = PICKLE_OK, argc = 0;
	char **argv = NULL;
	if (pickle_set_result_empty(i) != PICKLE_OK)
		return PICKLE_ERROR;
	picolParserInitialize(&p, o, eval, &i->line, &i->ch);
	int prevtype = p.type;
	for (;;) {
		if (picolGetToken(&p) != PICKLE_OK)
			return pickle_set_result_error(i, "Invalid parse");
		if (p.type == PT_EOF)
			break;
		int tlen = p.end - p.start + 1;
		if (tlen < 0)
			tlen = 0;
		char *t = picolMalloc(i, tlen + 1);
		if (!t) {
			retcode = PICKLE_ERROR;
			goto err;
		}
		move(t, p.start, tlen);
		t[tlen] = '\0';
		if (p.type == PT_VAR) {
			pickle_var_t * const v = picolGetVar(i, t, 1);
			if (!v) {
				retcode = pickle_set_result_error(i, "Invalid variable %s", t);
				(void)picolFree(i, t);
				goto err;
			}
			if (picolFree(i, t) != PICKLE_OK)
				goto err;
			if (!(t = picolStrdup(i, picolGetVarVal(v)))) {
				retcode = PICKLE_ERROR;
				goto err;
			}
		} else if (p.type == PT_CMD) {
			retcode = picolEvalAndSubst(i, NULL, t); // NB!
			if (picolFree(i, t) != PICKLE_OK)
				goto err;
			if (retcode != PICKLE_OK)
				goto err;
			if (!(t = picolStrdup(i, i->result))) {
				retcode = PICKLE_ERROR;
				goto err;
			}
		} else if (p.type == PT_ESC) {
			if (picolUnEscape(t, tlen + 1/*NUL terminator*/) < 0) {
				retcode = pickle_set_result_error(i, "Invalid escape sequence %s", t); /* BUG: %s is probably mangled by now */
				(void)picolFree(i, t);
				goto err;
			}
		} else if (p.type == PT_SEP) {
			prevtype = p.type;
			if (picolFree(i, t) != PICKLE_OK)
				goto err;
			continue;
		}

		if (p.type == PT_EOL) { /* We have a complete command + args. Call it! */
			if (picolFree(i, t) != PICKLE_OK)
				goto err;
			t = NULL;
			prevtype = p.type;
			if (p.o.noeval) {
				char *result = concatenate(i, " ", argc, argv, 0, 0);
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
			picolFreeArgList(i, argc, argv);
			argv = NULL;
			argc = 0;
			continue;
		}
	
		if (prevtype == PT_SEP || prevtype == PT_EOL) { /* New token, append to the previous or as new arg? */
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
			const int oldlen = picolStrlen(argv[argc - 1]), ilen = picolStrlen(t);
			char *arg = picolRealloc(i, argv[argc - 1], oldlen + ilen + 1);
			if (!arg) {
				retcode = PICKLE_ERROR;
				(void)picolFree(i, t);
				goto err;
			}
			argv[argc - 1] = arg;
			move(argv[argc - 1] + oldlen, t, ilen);
			argv[argc - 1][oldlen + ilen] = '\0';
		}
		if (picolFree(i, t) != PICKLE_OK)
			goto err;
		prevtype = p.type;
	}
err:
	picolFreeArgList(i, argc, argv);
	return retcode;
}

static int picolEval(pickle_t *i, const char *t) {
	assert(i);
	assert(t);
	return picolEvalAndSubst(i, NULL, t);
}

/*Based on: <http://c-faq.com/lib/regex.html>, also see:
 <https://www.cs.princeton.edu/courses/archive/spr09/cos333/beautiful.html> */
static inline int match(const char *pat, const char *str, size_t depth) {
	assert(pat);
	assert(str);
	if (!depth) return -1; /* error: depth exceeded */
 again:
        switch (*pat) {
	case '\0': return !*str;
	case '*': { /* match any number of characters: normally '.*' */
		const int r = match(pat + 1, str, depth - 1);
		if (r)         return r;
		if (!*(str++)) return 0;
		goto again;
	}
	case '?':  /* match any single characters: normally '.' */
		if (!*str) return 0;
		pat++, str++;
		goto again;
	case '%': /* escape character: normally backslash */
		if (!*(++pat)) return -2; /* error: missing escaped character */
		if (!*str)     return 0;
		/* fall through */
	default:
		if (*pat != *str) return 0;
		pat++, str++;
		goto again;
	}
	return -3; /* not reached */
}

static inline int isFalse(const char *s) {
	assert(s);
	static const char *negatory[] = { "0", "false", "off", "no", };
	for (size_t i = 0; i < (sizeof(negatory) / sizeof(negatory[0])); i++)
		if (!picolCompareCaseInsensitive(negatory[i], s))
			return 1;
	return 0;
}

static inline int isTrue(const char *s) {
	assert(s);
	static const char *affirmative[] = { "1", "true", "on", "yes", };
	for (size_t i = 0; i < (sizeof(affirmative) / sizeof(affirmative[0])); i++)
		if (!picolCompareCaseInsensitive(affirmative[i], s))
			return 1;
	return 0;
}

#define TRCC (256)

typedef struct { short set[TRCC]; /* x < 0 == delete, x | 0x100 == squeeze, x < 0x100 == translate */ } tr_t;

static inline int tr_init(tr_t *t, int translate, int compliment, const char *set1, const char *set2) {
	assert(t);
	assert(set1);
	assert(set2);
	char schoen[TRCC+1] = { 0 };

	if (compliment) {
		char haesslich[TRCC] = { 0 };
		for (unsigned char ch = 0; (ch = *set1); set1++)
			haesslich[ch] = 1;
		for (size_t i = 0, j = 0; i < sizeof haesslich; i++)
			if (haesslich[i] == 0)
				schoen[j++] = i;
		set1 = &schoen[1]; /* cannot deal with NUL */
	}

	for (size_t i = 0; i < TRCC; i++)
		t->set[i] = i;

	for (unsigned char from = 0; (from = *set1); set1++) {
		if (translate) {
			const unsigned char to = *set2;
			t->set[to]   |= 0x100; 
			t->set[from] = (t->set[from] & 0x100) | to;
			if (to && set2[1])
				set2++;
		} else { /* delete */
			t->set[from] = -1;
		}
	}
	return 0;
}

static inline int tr(const tr_t *t, const int squeeze, const char *in, const size_t inlen, char *out, size_t outlen) {
	assert(t);
	assert(in);
	assert(out);
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
	UNUSED(pd);
	assert(!pd);
	if (argc != 4 && argc != 5)
		return pickle_set_result_error_arity(i, 5, argc, argv);
	int compliment = 0, translate = 1, squeeze = 0;
	const char *op = argv[1], *set1 = argv[2];

	for (unsigned char ch = 0; (ch = *op); op++) {
		switch (ch) {
		case 'c': compliment = 1; break;
		case 's': squeeze    = 1; break;
		case 'd': translate  = 0; break;
		case 'r': translate  = 1; break;
		default:
			return pickle_set_result_error(i, "Invalid translate option");
		}
	}

	const char *set2 = argc == 4 ? set1 : argv[3];
       	const char *input = argv[3 + (argc == 5)];
	tr_t t = { .set = { 0 } };

	if (tr_init(&t, translate, compliment, set1, set2) < 0)
		return pickle_set_result_error(i, "Invalid translate option");
	const size_t ml = strlen(input) + 1;
	char *m = picolMalloc(i, ml);
	if (!m)
		return PICKLE_ERROR;
	int r = PICKLE_ERROR;
	if (tr(&t, squeeze, input, ml, m, ml) < 0)
		r = pickle_set_result_error(i, "Invalid translate");
	else
		r = pickle_set_result_string(i, m);
	if (picolFree(i, m) != PICKLE_OK)
		return PICKLE_ERROR;
	return r;
}

static inline int picolCommandString(pickle_t *i, const int argc, char **argv, void *pd) { /* Big! */
	UNUSED(pd);
	assert(!pd);
	if (argc < 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	const char *rq = argv[1];
	pickle_stack_or_heap_t h = { .p = NULL };
	if (argc == 3) {
		const char *arg1 = argv[2];
		if (!compare(rq, "trimleft"))
			return pickle_set_result_string(i, trimleft(string_white_space, arg1));
		if (!compare(rq, "trimright")) {
			const size_t l = picolStrlen(arg1);
			if (picolStackOrHeapAlloc(i, &h, l + 1) != PICKLE_OK)
				return -1;
			move(h.p, arg1, l + 1);
			trimright(string_white_space, h.p);
			const int r = pickle_set_result_string(i, h.p);
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!compare(rq, "trim"))      {
			const size_t l = picolStrlen(arg1);
			if (picolStackOrHeapAlloc(i, &h, l + 1) != PICKLE_OK)
				return -1;
			move(h.p, arg1, l + 1);
			const int r = pickle_set_result_string(i, trim(string_white_space, h.p));
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!compare(rq, "length"))
			return picolSetResultNumber(i, picolStrlen(arg1));
		if (!compare(rq, "toupper")) {
			if (picolStackOrHeapAlloc(i, &h, picolStrlen(arg1) + 1) != PICKLE_OK)
				return PICKLE_ERROR;
			size_t j = 0;
			for (j = 0; arg1[j]; j++)
				h.p[j] = toupper(arg1[j]);
			h.p[j] = 0;
			if (picolOnHeap(i, &h))
				return picolForceResult(i, h.p, 0);
			const int r = pickle_set_result_string(i, h.p);
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!compare(rq, "tolower")) {
			if (picolStackOrHeapAlloc(i, &h, picolStrlen(arg1) + 1) != PICKLE_OK)
				return PICKLE_ERROR;
			size_t j = 0;
			for (j = 0; arg1[j]; j++)
				h.p[j] = tolower(arg1[j]);
			h.p[j] = 0;
			if (picolOnHeap(i, &h))
				return picolForceResult(i, h.p, 0);
			const int r = pickle_set_result_string(i, h.p);
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!compare(rq, "reverse")) {
			/* NB. We could restructure so strlen and alloc are done upfront.
			 * The trade off is; less code, potentially more
			 * allocations. */
			const size_t l = picolStrlen(arg1);
			if (picolStackOrHeapAlloc(i, &h, picolStrlen(arg1) + 1) != PICKLE_OK)
				return PICKLE_ERROR;
			move(h.p, arg1, l + 1);
			const int r = pickle_set_result_string(i, reverse(h.p, l));
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!compare(rq, "ordinal"))
			return picolSetResultNumber(i, arg1[0]);
		if (!compare(rq, "char")) {
			number_t v = 0;
			if (picolStringToNumber(i, arg1, &v) != PICKLE_OK)
				return PICKLE_ERROR;
			char b[] = { v, 0 };
			return pickle_set_result_string(i, b);
		}
		if (!compare(rq, "dec2hex")) {
			number_t hx = 0;
			if (picolStringToNumber(i, arg1, &hx) != PICKLE_OK)
				return PICKLE_ERROR;
			BUILD_BUG_ON(SMALL_RESULT_BUF_SZ < PRINT_NUMBER_BUF_SZ);
			if (picolNumberToString(h.buf, hx, 16) != PICKLE_OK)
				return pickle_set_result_error(i, "Invalid conversion %s", h.buf);
			return pickle_set_result_string(i, h.buf);
		}
		if (!compare(rq, "hex2dec")) {
			number_t l = 0;
			if (picolConvertBaseNNumber(i, arg1, &l, 16) != PICKLE_OK)
				return pickle_set_result_error(i, "Invalid conversion %s", arg1);
			return picolSetResultNumber(i, l);
		}
		if (!compare(rq, "hash"))
			return picolSetResultNumber(i, picolHashString(arg1));
	} else if (argc == 4) {
		const char *arg1 = argv[2], *arg2 = argv[3];
		if (!compare(rq, "trimleft"))
			return pickle_set_result_string(i, trimleft(arg2, arg1));
		if (!compare(rq, "trimright")) {
			const size_t l = picolStrlen(arg1);
			if (picolStackOrHeapAlloc(i, &h, l + 1) != PICKLE_OK)
				return PICKLE_ERROR;
			move(h.p, arg1, l + 1);
			trimright(arg2, h.p);
			const int r = pickle_set_result_string(i, h.p);
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!compare(rq, "trim"))   {
			const size_t l = picolStrlen(arg1);
			if (picolStackOrHeapAlloc(i, &h, l + 1) != PICKLE_OK)
				return PICKLE_ERROR;
			move(h.p, arg1, l + 1);
			const int r = pickle_set_result_string(i, trim(arg2, h.p));
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!compare(rq, "match"))  {
			const int r = match(arg1, arg2, PICKLE_MAX_RECURSION - i->level);
			if (r < 0)
				return pickle_set_result_error(i, "Invalid regex %d", r);
			return picolSetResultNumber(i, r);
		}
		if (!compare(rq, "equal"))
			return picolSetResultNumber(i, !compare(arg1, arg2));
		if (!compare(rq, "unequal"))
			return picolSetResultNumber(i, !!compare(arg1, arg2));
		if (!compare(rq, "compare"))
			return picolSetResultNumber(i, compare(arg1, arg2));
		if (!compare(rq, "compare-no-case"))
			return picolSetResultNumber(i, picolCompareCaseInsensitive(arg1, arg2));
		if (!compare(rq, "index"))   {
			number_t index = 0;
			if (picolStringToNumber(i, arg2, &index) != PICKLE_OK)
				return PICKLE_ERROR;
			const number_t length = picolStrlen(arg1);
			if (index < 0)
				index = length + index;
			if (index > length)
				index = length - 1;
			if (index < 0)
				index = 0;
			const char ch[2] = { arg1[index], '\0' };
			return pickle_set_result_string(i, ch);
		}
		if (!compare(rq, "is")) {
			if (!compare(arg1, "alnum"))    { while (isalnum(*arg2))  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!compare(arg1, "alpha"))    { while (isalpha(*arg2))  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!compare(arg1, "digit"))    { while (isdigit(*arg2))  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!compare(arg1, "graph"))    { while (isgraph(*arg2))  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!compare(arg1, "lower"))    { while (islower(*arg2))  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!compare(arg1, "print"))    { while (isprint(*arg2))  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!compare(arg1, "punct"))    { while (ispunct(*arg2))  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!compare(arg1, "space"))    { while (isspace(*arg2))  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!compare(arg1, "upper"))    { while (isupper(*arg2))  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!compare(arg1, "xdigit"))   { while (isxdigit(*arg2)) arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!compare(arg1, "ascii"))    { while (*arg2 && !(0x80 & *arg2)) arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!compare(arg1, "control"))  { while (*arg2 && iscntrl(*arg2))  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!compare(arg1, "wordchar")) { while (isalnum(*arg2) || *arg2 == '_')  arg2++; return picolSetResultNumber(i, !*arg2); }
			if (!compare(arg1, "false"))    { return picolSetResultNumber(i, isFalse(arg2)); }
			if (!compare(arg1, "true"))     { return picolSetResultNumber(i, isTrue(arg2)); }
			if (!compare(arg1, "boolean"))  { return picolSetResultNumber(i, isTrue(arg2) || isFalse(arg2)); }
			if (!compare(arg1, "integer"))  { return picolSetResultNumber(i, picolStringToNumber(i, arg2, &(number_t){0l}) == PICKLE_OK); }
			/* Missing: double */
		}
		if (!compare(rq, "repeat")) {
			number_t count = 0, j = 0;
			const size_t length = picolStrlen(arg1);
			if (picolStringToNumber(i, arg2, &count) != PICKLE_OK)
				return PICKLE_ERROR;
			if (count < 0)
				return pickle_set_result_error(i, "Invalid range %ld", count);
			if (picolStackOrHeapAlloc(i, &h, (count * length) + 1) != PICKLE_OK)
				return PICKLE_ERROR;
			for (; j < count; j++) {
				implies(USE_MAX_STRING, (((j * length) + length) < PICKLE_MAX_STRING));
				move(&h.p[j * length], arg1, length);
			}
			h.p[j * length] = 0;
			const int r = pickle_set_result_string(i, h.p);
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!compare(rq, "first"))      {
			const char *found = find(arg2, arg1);
			if (!found)
				return picolSetResultNumber(i, -1);
			return picolSetResultNumber(i, found - arg2);
		}
		if (!compare(rq, "base2dec")) {
			number_t b = 0, n = 0;
			if (picolStringToNumber(i, arg2, &b) != PICKLE_OK)
				return PICKLE_ERROR;
			if (!picolIsBaseValid(b))
				return pickle_set_result_error(i, "Invalid base %ld", b);
			if (picolConvertBaseNNumber(i, arg1, &n, b) != PICKLE_OK)
				return pickle_set_result_error(i, "Invalid conversion %s", arg1);
			return picolSetResultNumber(i, n);
		}
		if (!compare(rq, "dec2base")) {
			number_t b = 0, n = 0;
			if (picolStringToNumber(i, arg2, &b) != PICKLE_OK)
				return PICKLE_ERROR;
			if (!picolIsBaseValid(b))
				return pickle_set_result_error(i, "Invalid base %ld", b);
			if (picolStringToNumber(i, arg1, &n) != PICKLE_OK)
				return pickle_set_result_error(i, "Invalid conversion %s", arg1);
			BUILD_BUG_ON(SMALL_RESULT_BUF_SZ < PRINT_NUMBER_BUF_SZ);
			if (picolNumberToString(h.buf, n, b) != PICKLE_OK)
				return pickle_set_result_error(i, "Invalid conversion %s", arg1);
			return pickle_set_result_string(i, h.buf);
		}
	} else if (argc == 5) {
		const char *arg1 = argv[2], *arg2 = argv[3], *arg3 = argv[4];
		if (!compare(rq, "first"))      {
			const number_t length = picolStrlen(arg2);
			number_t start  = 0;
			if (picolStringToNumber(i, arg3, &start) != PICKLE_OK)
				return PICKLE_ERROR;
			if (start < 0 || start >= length)
				return pickle_set_result_empty(i);
			const char *found = find(arg2 + start, arg1);
			if (!found)
				return picolSetResultNumber(i, -1);
			return picolSetResultNumber(i, found - arg2);
		}
		if (!compare(rq, "range")) {
			const number_t length = picolStrlen(arg1);
			number_t first = 0, last = 0;
			if (picolStringToNumber(i, arg2, &first) != PICKLE_OK)
				return PICKLE_ERROR;
			if (picolStringToNumber(i, arg3, &last) != PICKLE_OK)
				return PICKLE_ERROR;
			if (first > last)
				return pickle_set_result_empty(i);
			if (first < 0)
				first = 0;
			if (last > length)
				last = length;
			const number_t diff = (last - first) + 1;
			if (picolStackOrHeapAlloc(i, &h, diff) != PICKLE_OK)
				return PICKLE_ERROR;
			move(h.p, &arg1[first], diff);
			h.p[diff] = 0;
			const int r = pickle_set_result_string(i, h.p);
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!compare(rq, "tr"))
			return picolCommandTranslate(i, argc - 1, argv + 1, NULL);
	} else if (argc == 6) {
		const char *arg1 = argv[2], *arg2 = argv[3], *arg3 = argv[4], *arg4 = argv[5];
		if (!compare(rq, "replace")) {
			const number_t extend = picolStrlen(arg4);
			const number_t length = picolStrlen(arg1);
			number_t first = 0, last = 0;
			if (picolStringToNumber(i, arg2, &first) != PICKLE_OK)
				return PICKLE_ERROR;
			if (picolStringToNumber(i, arg3, &last) != PICKLE_OK)
				return PICKLE_ERROR;
			if (first < 0)
				first = 0;
			if (last > length)
				last = length;
			if (first > last || first > length || last < 0)
				return pickle_set_result_string(i, arg1);
			const number_t diff = (last - first) + 1;
			const number_t resulting = (length - diff) + extend + 1;
			assert(diff >= 0 && length >= 0);
			if (picolStackOrHeapAlloc(i, &h, resulting) != PICKLE_OK)
				return PICKLE_ERROR;
			move(h.p,                  arg1,            first);
			move(h.p + first,          arg4,            extend);
			move(h.p + first + extend, arg1 + last + 1, length - last);
			h.p[first + extend + length - last] = 0;
			if (picolOnHeap(i, &h))
				return picolForceResult(i, h.p, 0);
			const int r = pickle_set_result_string(i, h.p);
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!compare(rq, "tr"))
			return picolCommandTranslate(i, argc - 1, argv + 1, NULL);
	}
	return pickle_set_result_error_arity(i, 3, argc, argv);
}

static inline int picolCommandEqual(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc != 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	return pickle_set_result_integer(i, !!compare(argv[1], argv[2]));
}

static inline int picolCommandNotEqual(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc != 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	return pickle_set_result_integer(i, !compare(argv[1], argv[2]));
}

static int picolCommandIncr(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	number_t incr = 1, n = 0;
	if (argc != 2 && argc != 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	if (argc == 3)
		if (picolStringToNumber(i, argv[2], &incr) != PICKLE_OK)
			return PICKLE_ERROR;
	pickle_var_t *v = picolGetVar(i, argv[1], 1);
	if (!v)
		return pickle_set_result_error(i, "Invalid variable %s", argv[1]);
	const char *ns = picolGetVarVal(v);
	if (picolStringToNumber(i, ns, &n) != PICKLE_OK)
		return PICKLE_ERROR;
	n += incr;
	if (picolSetVarInteger(i, argv[1], n) != PICKLE_OK)
		return PICKLE_ERROR;
	return pickle_set_result_integer(i, n);
}

enum { UNOT, UINV, UABS, UBOOL, UNEGATE };
enum {
	BADD,  BSUB,    BMUL,    BDIV, BMOD,
	BMORE, BMEQ,    BLESS,   BLEQ, BEQ,
	BNEQ,  BLSHIFT, BRSHIFT, BAND, BOR,
	BXOR,  BMIN,    BMAX,    BPOW, BLOG
};

static inline int picolCommandMathUnary(pickle_t *i, const int argc, char **argv, void *pd) {
	if (argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	number_t a = 0;
	if (picolStringToNumber(i, argv[1], &a) != PICKLE_OK)
		return PICKLE_ERROR;
	switch ((intptr_t)(char*)pd) {
	case UNOT:    a = !a; break;
	case UINV:    a = ~a; break;
	case UABS:    a = a < 0 ? -a : a; break;
	case UBOOL:   a = !!a; break;
	case UNEGATE: a = -a; break;
	default: return pickle_set_result_error(i, "Invalid operator %s", argv[0]);
	}
	return picolSetResultNumber(i, a);
}

static inline int picolCommandMath(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	number_t a = 0, b = 0;
	if (picolStringToNumber(i, argv[1], &a) != PICKLE_OK)
		return PICKLE_ERROR;
	if (picolStringToNumber(i, argv[2], &b) != PICKLE_OK)
		return PICKLE_ERROR;
	number_t c = 0;
	switch ((intptr_t)(char*)pd) {
	case BADD:    c = a + b; break;
	case BSUB:    c = a - b; break;
	case BMUL:    c = a * b; break;
	case BDIV:    if (b) { c = a / b; } else { return pickle_set_result_error(i, "Invalid divisor"); } break;
	case BMOD:    if (b) { c = a % b; } else { return pickle_set_result_error(i, "Invalid divisor"); } break;
	case BMORE:   c = a > b; break;
	case BMEQ:    c = a >= b; break;
	case BLESS:   c = a < b; break;
	case BLEQ:    c = a <= b; break;
	case BEQ:     c = a == b; break;
	case BNEQ:    c = a != b; break;
	case BLSHIFT: c = ((unumber_t)a) << b; break;
	case BRSHIFT: c = ((unumber_t)a) >> b; break;
	case BAND:    c = a & b; break;
	case BOR:     c = a | b; break;
	case BXOR:    c = a ^ b; break;
	case BMIN:    c = MIN(a, b); break;
	case BMAX:    c = MAX(a, b); break;
	case BPOW:    if (picolPower(a, b, &c)     != PICKLE_OK) return pickle_set_result_error(i, "Invalid power"); break;
	case BLOG:    if (picolLogarithm(a, b, &c) != PICKLE_OK) return pickle_set_result_error(i, "Invalid logarithm"); break;
	default: return pickle_set_result_error(i, "Invalid operator %s", argv[0]);
	}
	return picolSetResultNumber(i, c);
}

static int picolCommandSet(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 3 && argc != 2)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	if (argc == 2) {
		const char *r = NULL;
		const int retcode = pickle_get_var_string(i, argv[1], &r);
		if (retcode != PICKLE_OK || !r)
			return pickle_set_result_error(i, "Invalid variable %s", argv[1]);
		return pickle_set_result_string(i, r);
	}
	if (pickle_set_var_string(i, argv[1], argv[2]) != PICKLE_OK)
		return PICKLE_ERROR;
	return pickle_set_result_string(i, argv[2]);
}

static int picolCommandCatch(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	return picolSetVarInteger(i, argv[2], picolEval(i, argv[1]));
}

static int picolCommandIf(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	int retcode = 0;
	if (argc != 3 && argc != 5)
		return pickle_set_result_error_arity(i, 5, argc, argv);
	if (argc == 5)
		if (compare("else", argv[3]))
			return pickle_set_result_error(i, "Invalid else %s", argv[3]);
	if ((retcode = picolEval(i, argv[1])) != PICKLE_OK)
		return retcode;
	if (!isFalse(i->result))
		return picolEval(i, argv[2]);
	else if (argc == 5)
		return picolEval(i, argv[4]);
	return PICKLE_OK;
}

static int picolCommandWhile(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc != 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	for (;;) {
		const int r1 = picolEval(i, argv[1]);
		if (r1 != PICKLE_OK)
			return r1;
		if (isFalse(i->result))
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
	UNUSED(pd);
	assert(!pd);
	if (argc < 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	args_t a = picolArgs(i, &(pickle_parser_opts_t){ 1, 1, 1, 1 }, argv[1]);
	if (!a.argv)
		return PICKLE_ERROR;
	if (a.argc != 2) {
		(void)picolFreeArgList(i, a.argc, a.argv);
		return pickle_set_result_error(i, "Invalid apply %s", argv[1]);
	}
	const int r = picolCommandCallProc(i, argc - 1, argv + 1, a.argv);
	if (picolFreeArgList(i, a.argc, a.argv) != PICKLE_OK)
		return PICKLE_ERROR;
	return r;
}

static int picolCommandFor(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc != 5)
		return pickle_set_result_error_arity(i, 5, argc, argv);
	const int r1 = picolEval(i, argv[1]);
	if (r1 != PICKLE_OK)
		return r1;
	for (;;) {
		const int r2 = picolEval(i, argv[2]);
		if (r2 != PICKLE_OK)
			return r2;
		if (isFalse(i->result))
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
	UNUSED(pd);
	assert(!pd);
	if (argc != 3)
		return pickle_set_result_error_arity(i, 4, argc, argv);
	number_t count = 0;
	if (picolStringToNumber(i, argv[1], &count) != PICKLE_OK)
		return PICKLE_ERROR;
	if (count < 0)
		return pickle_set_result_error(i, "Invalid argument %s", argv[1]);
	const int escape = picolStringNeedsEscaping(argv[2]);
	const size_t rl  = picolStrlen(argv[2]);
	char *escaped = escape ? picolEscape(i, argv[2], rl) : argv[2];
	const size_t el  = escape ? picolStrlen(escaped) : rl;
	char *r = picolMalloc(i, ((el + 1) * count) + 1);
	if (!r)
		goto fail;
	for (long j = 0; j < count; j++) {
		move(r + ((el + 1) * j), escaped, el);
		r[((el + 1) * j) + el] = j < (count - 1) ? ' ' : '\0';
	}
	r[count*el + count] = '\0';
	if (escaped != argv[2])
		picolFree(i, escaped);
	return picolForceResult(i, r, 0);
fail:
	if (escaped != argv[2])
		(void)picolFree(i, escaped);
	(void)picolFree(i, r);
	return PICKLE_ERROR;
}

static inline int picolCommandLLength(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	args_t a = picolArgs(i, &(pickle_parser_opts_t){ 1, 1, 1, 1 }, argv[1]);
	if (!a.argv)
		return PICKLE_ERROR;
	if (picolFreeArgList(i, a.argc, a.argv) != PICKLE_OK)
		return PICKLE_ERROR;
	return picolSetResultNumber(i, a.argc);
}

static inline int picolCommandLReverse(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	args_t a = picolArgs(i, &(pickle_parser_opts_t){ 1, 1, 1, 1 }, argv[1]);
	if (!a.argv)
		return PICKLE_ERROR;
	assert(a.argc >= 0);
	for (int j = 0; j < (a.argc / 2); j++)
		picolSwapString(&a.argv[j], &a.argv[(a.argc - j) - 1]);
	char *s = concatenate(i, " ", a.argc, a.argv, 1, 0);
	if (!s) {
		(void)picolFreeArgList(i, a.argc, a.argv);
		return PICKLE_ERROR;
	}
	if (picolFreeArgList(i, a.argc, a.argv) != PICKLE_OK)
		return PICKLE_ERROR;
	return picolForceResult(i, s, 0);
}

static inline int picolCommandLIndex(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc != 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	number_t index = 0;
	if (picolStringToNumber(i, argv[2], &index) != PICKLE_OK)
		return PICKLE_ERROR;
	args_t a = picolArgs(i, &(pickle_parser_opts_t){ 1, 1, 1, 1 }, argv[1]);
	if (!a.argv)
		return PICKLE_ERROR;
	assert(a.argc >= 0);
	if (!a.argc || index >= a.argc) {
		if (picolFreeArgList(i, a.argc, a.argv) != PICKLE_OK)
			return PICKLE_ERROR;
		return pickle_set_result_empty(i);
	}
	index = MAX(0, MIN(index, a.argc - 1));
	const int r1 = picolForceResult(i, a.argv[index], 0);
	a.argv[index] = NULL;
	const int r2 = picolFreeArgList(i, a.argc, a.argv);
	return r1 == PICKLE_OK && r2 == PICKLE_OK ? PICKLE_OK : PICKLE_ERROR;
}

enum { INSERT, DELETE, SET };

static inline int picolListOperation(pickle_t *i, const char *parse, const char *position, char *insert, int op, int doEsc) {
	assert(i);
	assert(parse);
	assert(position);
	assert(insert);
	const int escape = doEsc && picolStringNeedsEscaping(insert);
	const size_t il  = picolStrlen(insert);
	number_t index = 0, r = PICKLE_OK;
	if (picolStringToNumber(i, position, &index) != PICKLE_OK)
		return PICKLE_ERROR;
	if (!(insert = escape ? picolEscape(i, insert, il) : insert))
		return PICKLE_ERROR;
	char *prev = NULL;
	args_t a = picolArgs(i, &(pickle_parser_opts_t){ 1, 1, 1, 1 }, parse);
	assert(a.argc >= 0);
	if (!a.argv)
		return PICKLE_ERROR;
	if (!a.argc) {
		r = pickle_set_result_empty(i);
		goto done;
	}
	index = MAX(0, MIN(index, a.argc));
	if (op == INSERT) {
		if (!(a.argv = picolArgsGrow(i, &a.argc, a.argv))) {
			if (escape)
				(void)picolFree(i, insert);
			return PICKLE_ERROR;
		}
		if (index < (a.argc - 1))
			move(&a.argv[index + 1], &a.argv[index], sizeof (*a.argv) * (a.argc - index - 1));
		a.argv[index] = insert;
	} else if (op == SET) {
		prev = a.argv[index];
		a.argv[index] = insert;
	} else {
		assert(op == DELETE);
		prev = a.argv[index];
		a.argv[index] = NULL;
	}
done:
	if (r == PICKLE_OK) {
		char *s = concatenate(i, " ", a.argc, a.argv, 0, 0);
		if (!s)
			r = PICKLE_ERROR;
		else if (picolForceResult(i, s, 0) != PICKLE_OK)
			r = PICKLE_ERROR;
	}
	a.argv[index] = prev;

	if (escape) {
		if (picolFree(i, insert) != PICKLE_OK)
			r = PICKLE_ERROR;
	}
	if (picolFreeArgList(i, a.argc, a.argv) != PICKLE_OK)
		r = PICKLE_ERROR;
	return r;
}

static inline int picolDoLInsert(pickle_t *i, char *list, char *position, const int argc, char **argv, int doEsc) {
	assert(i);
	assert(list);
	assert(position);
	implies(argc >= 0, argv);
	char *insert = concatenate(i, " ", argc, argv, 0, 0);
	if (!insert)
		return PICKLE_ERROR;
	const int r1 = picolListOperation(i, list, position, insert, INSERT, doEsc);
	const int r2 = picolFree(i, insert);
	return r1 != PICKLE_OK || r2 != PICKLE_OK ? PICKLE_ERROR : PICKLE_OK;
}

static inline int picolCommandLInsert(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc < 4)
		return pickle_set_result_error_arity(i, 4, argc, argv);
	return picolDoLInsert(i, argv[1], argv[2], argc - 3, argv + 3, 1);
}

static inline int picolCommandLSet(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc == 3)
		return picolCommandSet(i, argc, argv, pd);
	if (argc != 4)
		return pickle_set_result_error_arity(i, 4, argc, argv);
	pickle_var_t *v = picolGetVar(i, argv[1], 1);
	if (!v)
		return pickle_set_result_error(i, "Invalid variable %s", argv[1]);
	if (picolListOperation(i, picolGetVarVal(v), argv[2], argv[3], argv[3][0] ? SET : DELETE, 1) != PICKLE_OK)
		return PICKLE_ERROR;
	return picolSetVarString(i, v,  i->result);
}

enum { INTEGER, STRING };

static inline int order(pickle_t *i, int op, int rev, const char *a, const char *b) { 
	assert(i);
	assert(a);
	assert(b);
	int r = 0;
	switch (op) {
	case INTEGER: {
		number_t an = 0, bn = 0;
		const int ra = picolStringToNumber(i, a, &an);
		const int rb = picolStringToNumber(i, b, &bn);
		if (ra != PICKLE_OK || rb != PICKLE_OK)
			return -1;
		r = an < bn;
		break;
	}
	case STRING: {
		r = compare(b, a) > 0;
		break;
	}
	}
	return rev ? !r : r;
}

static inline int sortArgs(pickle_t *i, int op, int rev, const int argc, char **argv) {
	assert(i);
	assert(argv);
	assert(argc >= 0);
	for (int j = 1, k = 0; j < argc; j++) { /* insertion sort */
		char *key = argv[j];
		k = j - 1;
		while (k >= 0) {
			const int od = order(i, op, rev, key, argv[k]);
			if (od < 0)
				return PICKLE_ERROR;
			if (!od)
				break;
			assert((k + 1) < argc && k < argc);
			argv[k + 1] = argv[k];
			k--;
		}
		assert((k + 1) < argc);
		argv[k + 1] = key;
	}
	return PICKLE_OK;
}

static inline int picolCommandLSort(pickle_t *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc < 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	int op = STRING, rev = 0, j = 1;
	for (j = 1; j < (argc - 1); j++) {
		if (!compare(argv[j], "-increasing"))
			rev = 0;
		else if (!compare(argv[j], "-decreasing"))
			rev = 1;
		else if (!compare(argv[j], "-ascii"))
			op = STRING;
		else if (!compare(argv[j], "-integer"))
			op = INTEGER;
		else
			return pickle_set_result_error(i, "Invalid option %s", argv[j]);
	}
	char *r = NULL;
       	args_t a = picolArgs(i, &(pickle_parser_opts_t){ 1, 1, 1, 1 }, argv[j]);
	if (!a.argv)
		return PICKLE_ERROR;
	if (sortArgs(i, op, rev, a.argc, a.argv) != PICKLE_OK)
		goto fail;
	r = concatenate(i, " ", a.argc, a.argv, 0, 0);
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
	UNUSED(pd);
	assert(!pd);
	if (argc < 4)
		return pickle_set_result_error_arity(i, 4, argc, argv);
	number_t first = 0, last = 0;
	if (picolStringToNumber(i, argv[2], &first) != PICKLE_OK)
		return PICKLE_ERROR;
	if (picolStringToNumber(i, argv[3], &last) != PICKLE_OK)
		return PICKLE_ERROR;
	if (last < first || first < 0) {
		char *args = concatenate(i, " ", argc - 4, argv + 4, 1, 0); 
		if (!args)
			return PICKLE_ERROR;
		const int r1 = picolDoLInsert(i, argv[1], argv[2], 1, (char *[1]) { args }, 0);
		const int r2 = picolFree(i, args);
		return r1 == PICKLE_OK && r2 == PICKLE_OK ? PICKLE_OK : PICKLE_ERROR;
	}
	int r = 0, empty = 0;
	args_t a = picolArgs(i, &(pickle_parser_opts_t){ 1, 1, 1, 1 }, argv[1]);
	char *repl = concatenate(i, " ", argc - 4, argv + 4, 1, 0), *n = NULL;
	if (!a.argv || !repl)
		goto err;
	empty = repl[0] == '\0';
	if (!empty && a.argc && first < 0)
		return pickle_set_result(i, "Invalid index %ld", first);
	for (size_t j = first; j <= (size_t)last && j < (size_t)a.argc; j++) {
		char *f = a.argv[j];
		a.argv[j] = NULL;
		if (picolFree(i, f) != PICKLE_OK)
			goto err;
		if (j == (size_t)first && !empty)
			a.argv[j] = repl;
	}
	if (!(n = concatenate(i, " ", a.argc, a.argv, 0, 0)))
		goto err;
	r = picolForceResult(i, n, 0);
	if (empty)
		if (picolFree(i, repl) != PICKLE_OK)
			r = PICKLE_ERROR;
	if (picolFreeArgList(i, a.argc, a.argv) != PICKLE_OK)
		return PICKLE_ERROR;
	return r;
err:
	(void)picolFreeArgList(i, a.argc, a.argv);
	(void)picolFree(i, repl);
	(void)picolFree(i, n);
	return PICKLE_ERROR;
}

/* implementing the '-all' option would be useful */
static inline int picolCommandLSearch(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc < 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	enum { oGLOB, oEXACT, oINTEGER };
	number_t start = 0, value = 0;
	int op = oGLOB, last = argc - 2, index = -1, not = 0, inl = 0;
	char *list = argv[argc - 2], *pattern = argv[argc - 1];
	for (int j = 1; j < last; j++) {
		     if (!compare(argv[j], "-integer")) { op = oINTEGER; } 
		else if (!compare(argv[j], "-exact"))   { op = oEXACT; } 
		else if (!compare(argv[j], "-inline"))  { inl = 1; } 
		else if (!compare(argv[j], "-not"))     { not = 1; } 
		else if (!compare(argv[j], "-glob"))    { op = oGLOB; }
		else if (!compare(argv[j], "-start")) {
			if (!((j + 1) < last))
				return pickle_set_result_error(i, "Invalid option %s", argv[j]);
			j++;
			if (picolStringToNumber(i, argv[j], &start) != PICKLE_OK)
				return PICKLE_ERROR;
		} else {
			return pickle_set_result_error(i, "Invalid option %s", argv[j]);
		}
	}
	if (op == oINTEGER)
		if (picolStringToNumber(i, pattern, &value) != PICKLE_OK)
			return PICKLE_ERROR;
	args_t a = picolArgs(i, &(pickle_parser_opts_t){ 1, 1, 1, 1 }, list);
	if (!a.argv)
		return PICKLE_ERROR;
	for (size_t j = start; j < (size_t)a.argc; j++) {
		switch (op) {
		case oGLOB: {
			const int m = match(pattern, a.argv[j], PICKLE_MAX_RECURSION - i->level);
			if (m < 0) {
				(void)picolFreeArgList(i, a.argc, a.argv);
				return pickle_set_result_error(i, "Invalid regex %d", m);
			}
			if (not ^ (m > 0)) {
				index = j;
				goto done;
			}
			break;
		}
		case oEXACT:
			if (not ^ !compare(pattern, a.argv[j])) {
				index = j;
				goto done;
			}
			break;
		case oINTEGER: {
			number_t n = 0;
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
			assert(op <= INTEGER);
		}
	}
done:
	if (inl && index > 0) {
		assert(index < a.argc);
		const int r = picolForceResult(i, a.argv[index], 0);
		a.argv[index] = NULL;
		(void)picolFreeArgList(i, a.argc, a.argv);
		return r;
	}
	(void)picolFreeArgList(i, a.argc, a.argv);
	return pickle_set_result_integer(i, index);
}

static inline int picolCommandLRange(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc != 4)
		return pickle_set_result_error_arity(i, 4, argc, argv);
	number_t first = 0, last = 0;
	if (picolStringToNumber(i, argv[2], &first) != PICKLE_OK)
		return PICKLE_ERROR;
	if (picolStringToNumber(i, argv[3], &last) != PICKLE_OK)
		return PICKLE_ERROR;
	if (first < 0)
		first = 0;
	if (last < 0)
		last = 0;
	int rs = 0;
	args_t a = picolArgs(i, &(pickle_parser_opts_t){ 1, 1, 1, 1 }, argv[1]);
	if (!a.argv)
		return PICKLE_ERROR;
	if (a.argc == 0) {
		if (picolFreeArgList(i, a.argc, a.argv) != PICKLE_OK)
			return PICKLE_ERROR;
		return pickle_set_result_empty(i);
	}
	if (last >= a.argc)
		last = a.argc - 1;
	char *range = concatenate(i, " ", 1 + last - first, a.argv + first, 1, 0);
	if (!range) {
		(void)picolFreeArgList(i, a.argc, a.argv);
		return PICKLE_ERROR;
	}
	rs = picolForceResult(i, range, 0);
	return picolFreeArgList(i, a.argc, a.argv) == PICKLE_OK ? rs : PICKLE_ERROR;
}

static inline int picolCommandLAppend(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc < 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	pickle_var_t *v = picolGetVar(i, argv[1], 1);
	const char *ovar = v ? picolGetVarVal(v) : NULL;
	char *nvar = NULL, *args = concatenate(i, " ", argc - 2, argv + 2, 1, 0);
	if (!args)
		return PICKLE_ERROR;
	char *list[2] = { (char*)ovar, args };
	nvar = concatenate(i, " ", 2 - (ovar == NULL), list + (ovar == NULL), 0, 0);
	const int r1 = picolFree(i, args);
	if (!nvar)
		return PICKLE_ERROR;
	const int r2 = pickle_set_var_string(i, argv[1], nvar);
	const int r3 = picolForceResult(i, nvar, 0);
	if (r1 != PICKLE_OK && r2 != PICKLE_OK && r3 != PICKLE_OK)
		return PICKLE_ERROR;
	return PICKLE_OK;
}

static inline int picolCommandSplit(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc != 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	const char *split = argv[1], *on = argv[2];
       	char **nargv = NULL, ch = split[0], *r = NULL;
	int nargc = 0, chars = !*on;
	if (!ch)
		return pickle_set_result_empty(i);
	for (; ch;) {
		size_t j = 0;
		if (chars) { /* special case, split on each character */
			ch = split[j];
			j = 1;
			if (!ch)
				break;
		} else { /* split on character set */
			for (; (ch = split[j]); j++)
				if (locateChar(on, ch))
					break;
		}
		char *t = picolMalloc(i, j + 1);
		if (!t)
			goto fail;
		move(t, split, j);
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
	if (!(r = concatenate(i, " ", nargc, nargv, 1, 0)))
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
	if (argc != 1)
		return pickle_set_result_error_arity(i, 1, argc, argv);
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
	assert(i);
	pickle_call_frame_t *cf = i->callframe;
	assert(i->level >= 0);
	i->level--;
	int r = PICKLE_OK;
	if (!cf)
		return PICKLE_OK;
	pickle_var_t *v = cf->vars, *t = NULL;
	while (v) {
		assert(v != v->next); /* Cycle? */
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
	assert(i);
	int r = PICKLE_OK;
	while (i->callframe)
		if (picolDropCallFrame(i) != PICKLE_OK)
			r = PICKLE_ERROR;
	return r;
}

static int picolCommandCallVariadic(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(pd);
	int errcode = PICKLE_OK;
	char **x = pd;
	if (i->level > (int)PICKLE_MAX_RECURSION)
		return pickle_set_result_error(i, "Invalid recursion %d", PICKLE_MAX_RECURSION);
	pickle_call_frame_t *cf = picolMalloc(i, sizeof(*cf));
	if (!cf)
		return PICKLE_ERROR;
	cf->vars     = NULL;
	cf->parent   = i->callframe;
	i->callframe = cf;
	i->level++;
	char *val = concatenate(i, " ", argc - 1, argv + 1, 1, 0);
	if (!val)
		goto error;
	if (pickle_set_var_string(i, x[0], val) != PICKLE_OK)
		goto error;
	errcode = picolEval(i, x[1]);
	if (picolDropCallFrame(i) != PICKLE_OK)
		errcode = PICKLE_ERROR;
	if (picolFree(i, val) != PICKLE_OK)
		errcode = PICKLE_ERROR;
	return errcode;
error:
	(void)picolDropCallFrame(i);
	(void)picolFree(i, val);
	return PICKLE_ERROR;
}

static int picolCommandCallProc(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(pd);
	if (i->level > (int)PICKLE_MAX_RECURSION)
		return pickle_set_result_error(i, "Invalid recursion %d", PICKLE_MAX_RECURSION);
	char **x = pd, *alist = x[0], *body = x[1], *tofree = NULL;
	char *p = picolStrdup(i, alist);
	int arity = 0, errcode = PICKLE_OK;
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
		if (++arity > (argc - 1))
			goto arityerr;
		if (pickle_set_var_string(i, start, argv[arity]) != PICKLE_OK) {
			(void)picolFree(i, tofree);
			(void)picolDropCallFrame(i);
			return PICKLE_ERROR;
		}
		p++;
	}
	if (picolFree(i, tofree) != PICKLE_OK)
		goto error;
	tofree = NULL;
	if (arity != (argc - 1))
		goto arityerr;
	errcode = picolEval(i, body);
	if (errcode == PICKLE_RETURN)
		errcode = PICKLE_OK;
	if (picolDropCallFrame(i) != PICKLE_OK)
		return PICKLE_ERROR;
	return errcode;
arityerr:
	(void)pickle_set_result_error(i, "Invalid argument count for %s", argv[0]);
error:
	picolFree(i, tofree);
	picolDropCallFrame(i);
	return PICKLE_ERROR;
}

static int picolCommandAddProc(pickle_t *i, const char *name, const char *args, const char *body, int variadic) {
	assert(i);
	assert(name);
	assert(args);
	assert(body);
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
	return pickle_register_command(i, name, variadic ? picolCommandCallVariadic : picolCommandCallProc, procdata);
}

static int picolCommandProc(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc != 4)
		return pickle_set_result_error_arity(i, 4, argc, argv);
	return picolCommandAddProc(i, argv[1], argv[2], argv[3], 0);
}

static int picolCommandVariadic(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc != 4)
		return pickle_set_result_error_arity(i, 4, argc, argv);
	return picolCommandAddProc(i, argv[1], argv[2], argv[3], 1);
}

static int picolCommandRename(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc != 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	return pickle_rename_command(i, argv[1], argv[2]);
}

static int picolCommandReturn(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 1 && argc != 2 && argc != 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	number_t retcode = PICKLE_RETURN;
	if (argc == 3)
		if (picolStringToNumber(i, argv[2], &retcode) != PICKLE_OK)
			return PICKLE_ERROR;
	if (argc == 1)
		return pickle_set_result_empty(i) != PICKLE_OK ? PICKLE_ERROR : PICKLE_RETURN;
	if (pickle_set_result_string(i, argv[1]) != PICKLE_OK)
		return PICKLE_ERROR;
	return retcode;
}

static int doJoin(pickle_t *i, const char *join, const int argc, char **argv, int list, int trim) {
	char *e = concatenate(i, join, argc, argv, list, trim);
	if (!e)
		return PICKLE_ERROR;
	return picolForceResult(i, e, 0);
}

static int picolCommandConcat(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	return doJoin(i, " ", argc - 1, argv + 1, 0, 1);
}

static int picolCommandList(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	return doJoin(i, " ", argc - 1, argv + 1, 1, 0);
}

static int picolCommandConjoin(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc < 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	return doJoin(i, argv[1], argc - 2, argv + 2, 0, 0);
}

static int picolCommandJoin(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	args_t a = picolArgs(i, &(pickle_parser_opts_t){ 1, 1, 1, 1 }, argv[1]);
	if (!a.argv)
		return PICKLE_ERROR;
	const int r = doJoin(i, argv[2], a.argc, a.argv, 0, 0);
	return picolFreeArgList(i, a.argc, a.argv) == PICKLE_OK ? r : PICKLE_ERROR;
}

static int picolCommandEval(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	int r = doJoin(i, " ", argc - 1, argv + 1, 0, 0);
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
	UNUSED(pd);
	pickle_parser_opts_t o = { 0, 0, 0, 1 };
again:
	if (argc < 2 || argc > 5)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	if (!compare(argv[1], "-nobackslashes")) {
		argc--, argv++;
		o.noescape = 1;
		goto again;
	}
	if (!compare(argv[1], "-novariables")) {
		argc--, argv++;
		o.novars = 1;
		goto again;
	}
	if (!compare(argv[1], "-nocommands")) {
		argc--, argv++;
		o.nocommands = 1;
		goto again;
	}
	if (argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	return picolEvalAndSubst(i, &o, argv[1]);
}

static int picolSetLevel(pickle_t *i, const char *levelStr) {
	const int top = levelStr[0] == '#';
	number_t level = 0;
	if (picolStringToNumber(i, top ? &levelStr[1] : levelStr, &level) != PICKLE_OK)
		return PICKLE_ERROR;
	if (top)
		level = i->level - level;
	if (level < 0)
		return pickle_set_result_error(i, "Invalid uplevel/upvar %d", level);

	for (int j = 0; j < level && i->callframe->parent; j++) {
		assert(i->callframe != i->callframe->parent);
		i->callframe = i->callframe->parent;
	}

	return PICKLE_OK;
}

static int picolCommandUpVar(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 4)
		return pickle_set_result_error_arity(i, 4, argc, argv);
	pickle_var_t *m = NULL, *o = NULL;
	pickle_call_frame_t *cf = i->callframe;
	int retcode = PICKLE_OK;
	if ((retcode = pickle_set_var_string(i, argv[3], "")) != PICKLE_OK) {
		pickle_set_result_error(i, "Invalid redefinition %s", argv[3]);
		goto end;
	}
	assert(cf);
	m = cf->vars;

	if ((retcode = picolSetLevel(i, argv[1])) != PICKLE_OK)
		goto end;
	if (!(o = picolGetVar(i, argv[2], 1))) {
		if (pickle_set_var_string(i, argv[2], "") != PICKLE_OK)
			return PICKLE_ERROR;
		o = i->callframe->vars;
	}

	if (m == o) { /* more advance cycle detection should be done here */
		pickle_set_result_error(i, "Invalid circular reference %s", argv[3]);
		goto end;
	}

	m->type = PV_LINK;
	m->data.link = o;
end:
	i->callframe = cf;
	return retcode;
}

static int picolCommandUpLevel(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc < 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	pickle_call_frame_t *cf = i->callframe;
	int retcode = picolSetLevel(i, argv[1]);
	if (retcode == PICKLE_OK) {
		char *e = concatenate(i, " ", argc - 2, argv + 2, 0, 0);
		if (!e) {
			retcode = PICKLE_ERROR;
			goto end;
		}
		const int inside_uplevel = i->inside_uplevel ;
		i->inside_uplevel = 1;
		retcode = picolEval(i, e);
		i->inside_uplevel = inside_uplevel;
		if (picolFree(i, e) != PICKLE_OK)
			retcode = PICKLE_ERROR;
	}
end:
	i->callframe = cf;
	return retcode;
}

static inline int picolUnsetVar(pickle_t *i, const char *name) {
	assert(i);
	assert(name);
	if (i->inside_uplevel)
		return pickle_set_result_error(i, "Invalid unset");
	pickle_call_frame_t *cf = i->callframe;
	pickle_var_t *p = NULL, *deleteMe = picolGetVar(i, name, 0/*NB!*/);
	if (!deleteMe)
		return pickle_set_result_error(i, "Invalid variable %s", name);

	if (cf->vars == deleteMe) {
		cf->vars = deleteMe->next;
		picolVarFree(i, deleteMe);
		return PICKLE_OK;
	}

	for (p = cf->vars; p->next != deleteMe && p; p = p->next)
		;
	assert(p->next == deleteMe);
	p->next = deleteMe->next;
	picolVarFree(i, deleteMe);
	return PICKLE_OK;
}

static int picolCommandUnSet(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	return picolUnsetVar(i, argv[1]);
}

/* returning a command list would be a good move */
static int picolCommandCommand(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc == 1) {
		long r = 0;
		for (long j = 0; j < i->length; j++) {
			pickle_command_t *c = i->table[j];
			for (; c; c = c->next) {
				r++;
				assert(c != c->next);
			}
		}
		return picolSetResultNumber(i, r);
	}
	if (argc == 2) {
		long r = -1, j = 0;
		for (long k = 0; k < i->length; k++) {
			pickle_command_t *c = i->table[k];
			for (; c; c = c->next) {
				if (!compare(argv[1], c->name)) {
					r = j;
					goto done;
				}
				j++;
			}
		}
	done:
		return picolSetResultNumber(i, r);
	}

	if (argc != 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	pickle_command_t *c = NULL;
	number_t r = 0, j = 0;
	if (picolStringToNumber(i, argv[2], &r) != PICKLE_OK)
		return PICKLE_ERROR;
	for (long k = 0; k < i->length; k++) {
		pickle_command_t *p = i->table[k];
		for (; p; p = p->next) {
			if (j == r) {
				c = p;
				goto located;
			}
			j++;
		}
	}
located:
	if (r != j || !c)
		return pickle_set_result_error(i, "Invalid index %ld", r);
	assert(c);
	const int defined = picolIsDefinedProc(c->func);
	const char *rq = argv[1];
	if (!compare(rq, "type")) {
		if (c->func == picolCommandCallProc)
			return pickle_set_result(i, "proc");
		if (c->func == picolCommandCallVariadic)
			return pickle_set_result(i, "variadic");
		return pickle_set_result(i, "built-in");
	}else if (!compare(rq, "args")) {
		if (!defined)
			return pickle_set_result(i, "%p", c->privdata);
		char **procdata = c->privdata;
		return pickle_set_result_string(i, procdata[0]);
	} else if (!compare(rq, "body")) {
		if (!defined)
			return pickle_set_result(i, "%p", c->func);
		char **procdata = c->privdata;
		return pickle_set_result_string(i, procdata[1]);
	} else if (!compare(rq, "name")) {
		return pickle_set_result_string(i, c->name);
	}
	return pickle_set_result_error(i, "Invalid subcommand %s", rq);
}

static int picolCommandInfo(pickle_t *i, const int argc, char **argv, void *pd) {
	if (argc < 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	const char *rq = argv[1];
	if (!compare(rq, "command"))
		return picolCommandCommand(i, argc - 1, argv + 1, pd);
	if (!compare(rq, "line"))
		return picolSetResultNumber(i, i->line);
	if (!compare(rq, "level"))
		return picolSetResultNumber(i, i->level);
	if (argc < 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	if (!compare(rq, "sizeof")) {
		rq = argv[2];
		if (!compare(rq, "pointer"))
			return picolSetResultNumber(i, CHAR_BIT * sizeof(char *));
		if (!compare(rq, "number"))
			return picolSetResultNumber(i, CHAR_BIT * sizeof(number_t));
	}
	if (!compare(rq, "limits")) {
		rq = argv[2];
		if (!compare(rq, "recursion"))
			return picolSetResultNumber(i, PICKLE_MAX_RECURSION);
		if (!compare(rq, "string"))
			return picolSetResultNumber(i, PICKLE_MAX_STRING);
		if (!compare(rq, "minimum"))
			return picolSetResultNumber(i, NUMBER_MIN);
		if (!compare(rq, "maximum"))
			return picolSetResultNumber(i, NUMBER_MAX);
	}
	if (!compare(rq, "features")) {
		rq = argv[2];
		if (!compare(rq, "allocator"))
			return picolSetResultNumber(i, DEFAULT_ALLOCATOR);
		if (!compare(rq, "string"))
			return picolSetResultNumber(i, DEFINE_STRING);
		if (!compare(rq, "maths"))
			return picolSetResultNumber(i, DEFINE_MATHS);
		if (!compare(rq, "debugging"))
			return picolSetResultNumber(i, DEBUGGING);
		if (!compare(rq, "strict"))
			return picolSetResultNumber(i, STRICT_NUMERIC_CONVERSION);
		if (!compare(rq, "string-length"))
			return picolSetResultNumber(i, USE_MAX_STRING ? PICKLE_MAX_STRING : -1);
	}
	return pickle_set_result_error(i, "Invalid subcommand %s", rq);
}

/* Regular Expression Engine
 * Modified from:
 * https://www.cs.princeton.edu/courses/archive/spr09/cos333/beautiful.html 
 *
 * Supports: "^$.*+?", escaping, and classes "\w\W\s\S\d\D"
 * Nice to have: hex escape sequences, ability to work on binary data. */

enum { /* watch out for those negatives */
	START =  '^', ESC    =  '\\', EOI  = '\0',
	END   = -'$', ANY    = -'.', MANY = -'*', ATLEAST = -'+', MAYBE  = -'?', 
	ALPHA = -'w', NALPHA = -'W',
	DIGIT = -'d', NDIGIT = -'D',
	SPACE = -'s', NSPACE = -'S',
};

enum { LAZY, GREEDY, POSSESSIVE };

/* escape a character, or return an operator */
static int regexEscape(const unsigned ch, const int esc) {
	switch (ch) {
	case -END: case -ANY: case -MANY: case -ATLEAST: case -MAYBE: 
		return esc ? ch : -ch;
	case 'w': return esc ? ALPHA  : 'w';
	case 'W': return esc ? NALPHA : 'W';
	case 'd': return esc ? DIGIT  : 'd';
	case 'D': return esc ? NDIGIT : 'D';
	case 's': return esc ? SPACE  : 's';
	case 'S': return esc ? NSPACE : 'S';
	case 'a': return esc ? '\a'   : 'a';
	case 'b': return esc ? '\b'   : 'b';
	case 'e': return esc ?  27    : 'e';
	case 'f': return esc ? '\f'   : 'f';
	case 'n': return esc ? '\n'   : 'n';
	case 'r': return esc ? '\r'   : 'r';
	case 't': return esc ? '\t'   : 't';
	case 'v': return esc ? '\v'   : 'v';
	case START: case ESC: case EOI: break;
	}
	return ch;
}

static int regexChar(const pickle_regex_t *x, const int pattern, const int ch) {
	assert(x);
	switch (pattern) {
	case ANY:    return 1;
	case ALPHA:  return  isalpha(ch); case NALPHA: return !isalpha(ch);
	case DIGIT:  return  isdigit(ch); case NDIGIT: return !isdigit(ch);
	case SPACE:  return  isspace(ch); case NSPACE: return !isspace(ch);
	}
	if (x->nocase)
		return tolower(pattern) == tolower(ch);
	return pattern == ch;
}

static int regexStar(pickle_regex_t *x, int depth, int c, const char *regexp, const char *text);

/* search for regexp at beginning of text */
static int regexHere(pickle_regex_t *x, const int depth, const char *regexp, const char *text) {
	assert(x);
	assert(regexp);
	assert(text);
	if (x->max && depth > x->max)
		return -1;
	int r1 = EOI, r2 = EOI;
again:
	r1 = regexEscape(regexp[0], 0);
	if (r1 == EOI) {
		x->end = text;
		return 1;
	}
	if (r1 == START)
		return -1;
	if (r1 == ESC) {
		r1 = regexEscape(regexp[1], 1);
		if (r1 == EOI)
			return -1;
		regexp++;
	}
	r2 = regexEscape(regexp[1], 0);
	if (r2 == MAYBE) {
		const int is = regexChar(x, r1, *text);
		if (x->type == GREEDY) {
			if (is) {
				const int m = regexHere(x, depth + 1, regexp + 2, text + 1);
				if (m)
					return m;
			}
			regexp += 2;
			goto again;
		} else if (x->type == LAZY) {
			const int m = regexHere(x, depth + 1, regexp + 2, text);
			if (m)
				return m;
			if (!is)
				return 0;
			regexp += 2, text++;
			goto again;
		} else {
			assert(x->type == POSSESSIVE);
			regexp += 2, text += is;
			goto again;
		}
	}
	if (r2 == ATLEAST) {
		if (!regexChar(x, r1, *text))
			return 0;
		return regexStar(x, depth + 1, r1, regexp + 2, text + 1);
	}
	if (r2 == MANY)
		return regexStar(x, depth + 1, r1, regexp + 2, text);
	if (r1 == END) {
		if (r2 != EOI)
			return -1;
		return *text == EOI;
	}
	if (*text != EOI && regexChar(x, r1, *text)) {
		regexp++, text++;
		goto again;
	}
	return 0;
}

/* search for c*regexp at beginning of text */
static int regexStar(pickle_regex_t *x, const int depth, const int c, const char *regexp, const char *text) {
	assert(x);
	assert(regexp);
	assert(text);
	if (x->max && depth > x->max)
		return -1;
	if (x->type == GREEDY || x->type == POSSESSIVE) {
		const char *t = NULL;
		for (t = text; *t != EOI && regexChar(x, c, *t); t++)
			;
		if (x->type == POSSESSIVE)
			return regexHere(x, depth + 1, regexp, t);
		do {
			const int m = regexHere(x, depth + 1, regexp, t);
			if (m)
				return m;
		} while (t-- > text);
		return 0;
	}
	assert(x->type == LAZY);
	do {    /* a '*' matches zero or more instances */
		const int m = regexHere(x, depth + 1, regexp, text);
		if (m)
			return m;
	} while (*text != EOI && regexChar(x, c, *text++));
	return 0;
}

/* search for regexp anywhere in text */
static int picolRegexExtract(pickle_regex_t *x, const char *regexp, const char *text) {
	assert(x);
	assert(regexp);
	assert(text);
	x->start = NULL;
	x->end   = NULL;
	if (regexp[0] == START)
		return regexHere(x, 0, regexp + 1, text);
	do {    /* must look even if string is empty */
		const int m = regexHere(x, 0, regexp, text);
		if (m) {
			if (m > 0)
				x->start = text;
			return m;
		}
	} while (*text++ != EOI);
	x->start = NULL;
	x->end   = NULL;
	return 0;
}

static int picolRegex(const char *regexp, const char *text) {
	assert(regexp);
	assert(text);
	pickle_regex_t x = { NULL, NULL, PICKLE_MAX_RECURSION, LAZY, 0 };
	return picolRegexExtract(&x, regexp, text);
}

static inline int picolCommandRegex(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc < 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	number_t index = 0;
	unsigned type = GREEDY, nocase = 0;	
	const int last = argc - 2;
	for (int j = 1; j < last; j++) {
		if (!compare(argv[j], "-nocase")) {
			nocase = 1;
		} else if (!compare(argv[j], "-possessive")) {
			type = POSSESSIVE;
		} else if (!compare(argv[j], "-lazy")) {
			type = LAZY;
		} else if (!compare(argv[j], "-greedy")) {
			type = GREEDY;
		} else if (!compare(argv[j], "-start")) {
			if (!((j + 1) < last))
				return pickle_set_result_error(i, "Invalid option %s", argv[j]);
			j++;
			if (picolStringToNumber(i, argv[j], &index) != PICKLE_OK)
				return PICKLE_ERROR;
		} else {
			return pickle_set_result_error(i, "Invalid option %s", argv[j]);
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
	pickle_regex_t x = { NULL, NULL, PICKLE_MAX_RECURSION, type, nocase };
	mutual(x.start, x.end);
	const int r = picolRegexExtract(&x, pattern, string);
	if (r < 0)
		return pickle_set_result_error(i, "Invalid regex %s", pattern);
	if (r == 0)
		return pickle_set_result_string(i, "-1 -1");
	assert(x.start);
	assert(x.end);
	implies(x.start, x.start >= orig);
	implies(x.end,   x.end   >= x.start);
	number_t start = x.start - orig, end = x.end - orig;
	end -= (end != start);
	return pickle_set_result(i, "%ld %ld", (long)start, (long)end);
}

static int picolRegisterCoreCommands(pickle_t *i) {
	assert(i);

	typedef PREPACK struct {
		const char *name;             /**< Name of function/TCL command */
		pickle_command_func_t func;   /**< Callback that actually does stuff */
		void *data;                   /**< Optional data for this function */
	} POSTPACK pickle_register_command_t; /**< A single TCL command */

	static const pickle_register_command_t commands[] = {
		{ "ne",        picolCommandEqual,     NULL },
		{ "eq",        picolCommandNotEqual,  NULL },
		{ "incr",      picolCommandIncr,      NULL },
		{ "break",     picolCommandRetCodes,  (char*)PICKLE_BREAK },
		{ "catch",     picolCommandCatch,     NULL },
		{ "concat",    picolCommandConcat,    NULL },
		{ "continue",  picolCommandRetCodes,  (char*)PICKLE_CONTINUE },
		{ "eval",      picolCommandEval,      NULL },
		{ "for",       picolCommandFor,       NULL },
		{ "if",        picolCommandIf,        NULL },
		{ "info",      picolCommandInfo,      NULL },
		{ "join",      picolCommandJoin,      NULL },
		{ "conjoin",   picolCommandConjoin,   NULL },
		{ "list",      picolCommandList,      NULL },
		{ "proc",      picolCommandProc,      NULL },
		{ "variadic",  picolCommandVariadic,  NULL },
		{ "rename",    picolCommandRename,    NULL },
		{ "return",    picolCommandReturn,    NULL },
		{ "set",       picolCommandSet,       NULL },
		{ "subst",     picolCommandSubst,     NULL },
		{ "unset",     picolCommandUnSet,     NULL },
		{ "uplevel",   picolCommandUpLevel,   NULL },
		{ "upvar",     picolCommandUpVar,     NULL },
		{ "while",     picolCommandWhile,     NULL },
		{ "apply",     picolCommandApply,     NULL },
	};
	if (DEFINE_REGEX) {
		if (picolRegisterCommand(i, "reg", picolCommandRegex, NULL) != PICKLE_OK)
			return PICKLE_ERROR;
	}
	if (DEFINE_STRING)
		if (picolRegisterCommand(i, "string", picolCommandString, NULL) != PICKLE_OK)
			return PICKLE_ERROR;
	if (DEFINE_LIST) {
		pickle_register_command_t list[] = {
			{ "lindex",    picolCommandLIndex,    NULL },
			{ "lreverse",  picolCommandLReverse,  NULL },
			{ "llength",   picolCommandLLength,   NULL },
			{ "linsert",   picolCommandLInsert,   NULL },
			{ "lrange",    picolCommandLRange,    NULL },
			{ "lsearch",   picolCommandLSearch,   NULL },
			{ "lset",      picolCommandLSet,      NULL },
			{ "lrepeat",   picolCommandLRepeat,   NULL },
			{ "lsort",     picolCommandLSort,     NULL },
			{ "lreplace",  picolCommandLReplace,  NULL },
			{ "lappend",   picolCommandLAppend,   NULL },
			{ "split",     picolCommandSplit,     NULL },
		};
		for (size_t j = 0; j < sizeof(list)/sizeof(list[0]); j++)
			if (picolRegisterCommand(i, list[j].name, list[j].func, list[j].data) != PICKLE_OK)
				return PICKLE_ERROR;
	}
	if (DEFINE_MATHS) {
		static const char *unary[]  = { [UNOT] = "not", [UINV] = "invert", [UABS] = "abs", [UBOOL] = "bool", [UNEGATE] = "negate" };
		static const char *binary[] = {
			[BADD]   =  "+",   [BSUB]     =  "-",      [BMUL]     =  "*",      [BDIV]  =  "/",    [BMOD]  =  "mod",
			[BMORE]  =  ">",   [BMEQ]     =  ">=",     [BLESS]    =  "<",      [BLEQ]  =  "<=",   [BEQ]   =  "==",
			[BNEQ]   =  "!=",  [BLSHIFT]  =  "lshift", [BRSHIFT]  =  "rshift", [BAND]  =  "and",  [BOR]   =  "or",
			[BXOR]   =  "xor", [BMIN]     =  "min",    [BMAX]     =  "max",    [BPOW]  =  "pow",  [BLOG]  =  "log"
		};
		for (size_t j = 0; j < sizeof(unary)/sizeof(char*); j++)
			if (picolRegisterCommand(i, unary[j], picolCommandMathUnary, (char*)(intptr_t)j) != PICKLE_OK)
				return PICKLE_ERROR;
		for (size_t j = 0; j < sizeof(binary)/sizeof(char*); j++)
			if (picolRegisterCommand(i, binary[j], picolCommandMath, (char*)(intptr_t)j) != PICKLE_OK)
				return PICKLE_ERROR;
	}
	for (size_t j = 0; j < sizeof(commands)/sizeof(commands[0]); j++)
		if (picolRegisterCommand(i, commands[j].name, commands[j].func, commands[j].data) != PICKLE_OK)
			return PICKLE_ERROR;
	return picolSetVarInteger(i, "version", VERSION);
}

static int picolFreeCmd(pickle_t *i, pickle_command_t *p) {
	assert(i);
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
	assert(i);
	picolDropAllCallFrames(i);
	assert(!(i->callframe));
	int r = PICKLE_OK;
	if (picolFreeResult(i) != PICKLE_OK)
		r = PICKLE_ERROR;
	for (long j = 0; j < i->length; j++) {
		pickle_command_t *c = i->table[j], *p = NULL;
		for (; c; p = c, c = c->next) {
			if (picolFreeCmd(i, p) != PICKLE_OK)
				r = PICKLE_ERROR;
			assert(c != c->next);
		}
		if (picolFreeCmd(i, p) != PICKLE_OK)
			r = PICKLE_ERROR;
	}
	if (picolFree(i, i->table) != PICKLE_OK)
		r = PICKLE_ERROR;
	zero(i, sizeof *i);
	return r;
}

static int picolInitialize(pickle_t *i, const pickle_allocator_t *a) {
	static_assertions();
	assert(i);
	assert(a);
	/*'i' may contain junk, otherwise: assert(!(i->initialized));*/
	const size_t hbytes = PICKLE_MAX_STRING;
	const size_t helem  = hbytes / sizeof (*i->table);
	zero(i, sizeof *i);
	i->initialized   = 1;
	i->allocator     = *a;
	i->callframe     = i->allocator.malloc(i->allocator.arena, sizeof(*i->callframe));
	i->result        = string_empty;
	i->static_result = 1;
	i->table         = picolMalloc(i, hbytes); /* NB. We could make this configurable, for little gain. */

	if (!(i->callframe) || !(i->result) || !(i->table))
		goto fail;
	zero(i->table,     hbytes);
	zero(i->callframe, sizeof(*i->callframe));
	i->length = helem;
	if (picolRegisterCoreCommands(i) != PICKLE_OK)
		goto fail;
	if (pickle_set_var_string(i, "argv", "") != PICKLE_OK)
		goto fail;
	return PICKLE_OK;
fail:
	(void)picolDeinitialize(i);
	return PICKLE_ERROR;
}

static inline void *pmalloc(void *arena, const size_t size) {
	UNUSED(arena); assert(arena == NULL);
	return malloc(size);
}

static inline void *prealloc(void *arena, void *ptr, const size_t size) {
	UNUSED(arena); assert(arena == NULL);
	return realloc(ptr, size);
}

static inline int pfree(void *arena, void *ptr) {
	UNUSED(arena); assert(arena == NULL);
	free(ptr);
	return PICKLE_OK;
}

static inline int test(const char *eval, const char *result, int retcode) {
	assert(eval);
	assert(result);
	int r = 0, actual = 0;
	pickle_t *p = NULL;
	const int rc = pickle_new(&p, NULL);
	if (rc != PICKLE_OK || !p)
		return -1;
	if ((actual = picolEval(p, eval)) != retcode) { r = -2; goto end; }
	if (!(p->result))                             { r = -3; goto end; }
	if (compare(p->result, result))               { r = -4; goto end; }
end:
	pickle_delete(p);
	return r;
}

static inline int picolTestSmallString(void) {
	int r = 0;
	if (!picolIsSmallString(""))  { r = -1; }
	if (!picolIsSmallString("0")) { r = -2; }
	if (picolIsSmallString("Once upon a midnight dreary")) { r = -3; }
	return r;
}

static inline int picolTestUnescape(void) {
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
		zero(m, sizeof m); /* lazy */
		strncpy(m, ts[i].str, sizeof(m) - 1);
		const int u = picolUnEscape(m, picolStrlen(m) + 1);
		if (ts[i].r != u) {
			r = -1;
			continue;
		}
		if (u < 0) {
			continue;
		}
		if (compare(m, ts[i].res)) {
			r = -2;
			continue;
		}
	}
	return r;
}

static inline int concatenateTest(pickle_t *i, char *result, char *join, int argc, char **argv) {
	int r = PICKLE_OK;
	char *f = NULL;
	if (!(f = concatenate(i, join, argc, argv, 0, 0)) || compare(f, result))
		r = PICKLE_ERROR;
	picolFree(i, f);
	return r;
}

static inline int picolTestConcat(void) {
	int r = 0;
	pickle_t *p = NULL;
	if (pickle_new(&p, NULL) != PICKLE_OK || !p)
		return -100;
	r += concatenateTest(p, "ac",    "",  2, (char*[2]){"a", "c"});
	r += concatenateTest(p, "a,c",   ",", 2, (char*[2]){"a", "c"});
	r += concatenateTest(p, "a,b,c", ",", 3, (char*[3]){"a", "b", "c"});
	r += concatenateTest(p, "a",     "X", 1, (char*[1]){"a"});
	r += concatenateTest(p, "",      "",  0, NULL);

	if (pickle_delete(p) != PICKLE_OK)
		r = -10;
	return r;
}

static inline int picolTestEval(void) {
	static const struct test_t {
		int retcode;
		char *eval, *result;
	} ts[] = {
		{ PICKLE_OK,    "+  2 2",          "4"     },
		{ PICKLE_OK,    "* -2 9",          "-18"   },
		{ PICKLE_OK,    "join {a b c} ,",  "a,b,c" },
		{ PICKLE_ERROR, "return fail -1",  "fail"  },
	};

	int r = 0;
	for (size_t i = 0; i < sizeof(ts)/sizeof(ts[0]); i++)
		if (test(ts[i].eval, ts[i].result, ts[i].retcode) < 0)
			r = -(int)(i+1);
	return r;
}

static inline int picolTestLineNumber(void) {
	static const struct test_t {
		int line;
		char *eval;
	} ts[] = {
		{ 1, "+  2 2", },
		{ 2, "+  2 2\n", },
		{ 3, "\n\n\n", },
		{ 4, "* 4 4\nset a 3\n\n", },
		{ 3, "* 4 4\r\nset a 3\r\n", },
	};

	int r = 0;
	for (size_t i = 0; i < sizeof(ts)/sizeof(ts[0]); i++) {
		pickle_t *p = NULL;
		if (pickle_new(&p, NULL) != PICKLE_OK || !p) {
			r = r ? r : -1001;
			break;
		}
		if (pickle_eval(p, ts[i].eval) != PICKLE_OK)
			r = r ? r : -2001;

		if (p->line != ts[i].line)
			r = r ? r : -(int)(i+1);

		if (pickle_delete(p) != PICKLE_OK)
			r = r ? r : -4001;
	}
	return r;
}

static inline int picolTestConvertNumber(void) {
	int r = 0;
	number_t val = 0;
	pickle_t *p = NULL;

	static const struct test_t {
		number_t val;
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

	if (pickle_new(&p, NULL) != PICKLE_OK || !p)
		return -1;

	for (size_t i = 0; i < sizeof(ts)/sizeof(ts[0]); i++) {
		const struct test_t *t = &ts[i];
		val = 0;
		const int error = picolStringToNumber(p, t->string, &val);
		const int fail = (t->val != val || t->error != error);
		if (fail)
			r = -2;
	}

	if (pickle_delete(p) != PICKLE_OK)
		r = -3;
	return r;
}

static inline int picolTestGetSetVar(void) {
	long val = 0;
	int r = 0;
	pickle_t *p = NULL;
	if (pickle_new(&p, NULL) != PICKLE_OK || !p)
		return -1;
	if (pickle_eval(p, "set a 54; set b 3; set c -4x") != PICKLE_OK)
		r = -2;
	if (pickle_get_var_integer(p, "a", &val) != PICKLE_OK || val != 54)
		r = -3;
	if (pickle_get_var_integer(p, "c", &val) == PICKLE_OK || val != 0)
		r = -4;
	if (pickle_set_var_string(p, "d", "123") != PICKLE_OK)
		r = -5;
	if (pickle_get_var_integer(p, "d", &val) != PICKLE_OK || val != 123)
		r = -6;
	if (pickle_delete(p) != PICKLE_OK)
		r = -7;
	return r;
}

static inline int picolTestParser(void) {
	int r = 0;
	pickle_parser_t p = { .p = NULL };

	static const struct test_t {
		char *text;
		int line;
	} ts[] = {
		{ "$a", 2 },
		{ "\"a b c\"", 2 },
		{ "a  b c {a b c}", 2 },
		{ "[+ 2 2]", 2 },
		{ "[+ 2 2]; $a; {v}", 2 },
	};

	for (size_t i = 0; i < sizeof(ts)/sizeof(ts[0]); i++) {
		const char *ch = NULL;
		int line = 1;
		picolParserInitialize(&p, NULL, ts[i].text, &line, &ch);
		do {
			if (picolGetToken(&p) == PICKLE_ERROR)
				break;
			assert(p.start && p.end);
			assert(p.type <= PT_EOF);
		} while (p.type != PT_EOF);
	}
	return r;
}

static int picolTestRegex(void) {
	int r = 0;
	r += !(1 == picolRegex("a", "bba"));
	r += !(1 == picolRegex(".", "x"));
	r += !(1 == picolRegex("\\.", "."));
	r += !(0 == picolRegex("\\.", "x"));
	r += !(0 == picolRegex(".", ""));
	r += !(0 == picolRegex("a", "b"));
	r += !(1 == picolRegex("^a*b$", "b"));
	r += !(0 == picolRegex("^a*b$", "bx"));
	r += !(1 == picolRegex("a*b", "b"));
	r += !(1 == picolRegex("a*b", "ab"));
	r += !(1 == picolRegex("a*b", "aaaab"));
	r += !(1 == picolRegex("a*b", "xaaaab"));
	r += !(0 == picolRegex("^a*b", "xaaaab"));
	r += !(1 == picolRegex("a*b", "xaaaabx"));
	r += !(1 == picolRegex("a*b", "xaaaaxb"));
	r += !(0 == picolRegex("a*b", "xaaaax"));
	r += !(0 == picolRegex("a$", "ab"));
	r += !(1 == picolRegex("a*", ""));
	r += !(1 == picolRegex("a*", "a"));
	r += !(1 == picolRegex("a*", "aa"));
	r += !(1 == picolRegex("a+", "a"));
	r += !(0 == picolRegex("a+", ""));
	r += !(1 == picolRegex("ca?b", "cab"));
	r += !(1 == picolRegex("ca?b", "cb"));
	r += !(1 == picolRegex("\\sz", " \t\r\nz"));
	r += !(0 == picolRegex("\\s", "x"));
	return -r;
}

static inline void pre(pickle_t *i) { /* assert API pre-conditions */
	/* We could assert all the data structures, such as the hash 
	 * table and the call stack, also have valid data */
	assert(i);
	assert(i->initialized);
	assert(i->result);
	assert(i->length > 0);
	assert(!!locateByte(i->result_buf, 0, sizeof i->result_buf));
	assert(i->level >= 0);
}

static inline int post(pickle_t *i, const int r) { /* assert API post-conditions */
	pre(i);
	assert(r >= PICKLE_ERROR && r <= PICKLE_CONTINUE);
	return r;
}

unsigned long pickle_version(void) {
	unsigned long info = 0;
	info |= DEFINE_TESTS              << 0;
	info |= DEFINE_MATHS              << 1;
	info |= DEFINE_STRING             << 2;
	info |= DEFINE_LIST               << 3;
	info |= DEFINE_REGEX              << 4;
	info |= DEFAULT_ALLOCATOR         << 5;
	info |= USE_MAX_STRING            << 6;
	info |= STRICT_NUMERIC_CONVERSION << 7;
	return (info << 24) | PICKLE_VERSION;
}

int pickle_set_result_string(pickle_t *i, const char *s) {
	pre(i);
	assert(s);
	int is_static = 1;
	char *r = i->result_buf;
	const size_t sl = picolStrlen(s) + 1;
	if (sizeof(i->result_buf) < sl) {
		is_static = 0;
		r = picolMalloc(i, sl);
		if (!r)
			return post(i, PICKLE_ERROR);
	}
	move(r, s, sl);
	const int fr = picolFreeResult(i);
	i->static_result = is_static;
	i->result = r;
	return post(i, fr);
}

int pickle_get_result_string(pickle_t *i, const char **s) {
	pre(i);
	assert(s);
	*s = i->result;
	return post(i, PICKLE_OK);
}

int pickle_get_result_integer(pickle_t *i, long *val) {
	pre(i);
	assert(val);
	number_t v = 0;
	const int r = picolStringToNumber(i, i->result, &v);
	*val = v;
	return post(i, r);
}

int pickle_set_result_empty(pickle_t *i) {
	pre(i);
	return post(i, picolForceResult(i, string_empty, 1));
}

int pickle_register_command(pickle_t *i, const char *name, pickle_command_func_t func, void *privdata) {
	pre(i);
	assert(name);
	assert(func);
	return post(i, picolRegisterCommand(i, name, func, privdata));
}

int pickle_set_var_string(pickle_t *i, const char *name, const char *val) {
	pre(i);
	assert(name);
	assert(val);
	pickle_var_t *v = picolGetVar(i, name, 1);
	if (v) {
		picolFreeVarVal(i, v);
		if (picolSetVarString(i, v, val) != PICKLE_OK)
			return post(i, PICKLE_ERROR);
	} else {
		if (!(v = picolMalloc(i, sizeof(*v))))
			return post(i, PICKLE_ERROR);
		zero(v, sizeof *v);
		const int r1 = picolSetVarName(i, v, name);
		const int r2 = picolSetVarString(i, v, val);
		if (r1 != PICKLE_OK || r2 != PICKLE_OK) {
			(void)picolFreeVarName(i, v);
			(void)picolFreeVarVal(i, v);
			(void)picolFree(i, v);
			return post(i, PICKLE_ERROR);
		}
		v->next = i->callframe->vars;
		i->callframe->vars = v;
	}
	return post(i, PICKLE_OK);
}

int pickle_get_var_string(pickle_t *i, const char *name, const char **val) {
	pre(i);
	assert(name);
	assert(val);
	*val = NULL;
	pickle_var_t *v = picolGetVar(i, name, 1);
	if (!v)
		return post(i, PICKLE_ERROR);
	*val = picolGetVarVal(v);
	return post(i, *val ? PICKLE_OK : PICKLE_ERROR);
}

int pickle_get_var_integer(pickle_t *i, const char *name, long *val) {
	pre(i);
	assert(name);
	assert(val);
	*val = 0;
	const char *s = NULL;
	const int retcode = pickle_get_var_string(i, name, &s);
	if (!s || retcode != PICKLE_OK)
		return post(i, PICKLE_ERROR);
	number_t v = 0;
	const int r = picolStringToNumber(i, s, &v);
	*val = v;
	return post(i, r);
}

int pickle_set_result_error(pickle_t *i, const char *fmt, ...) {
	pre(i);
	assert(fmt);
	va_list ap;
	va_start(ap, fmt);
	char *r = picolVsprintf(i, fmt, ap);
	va_end(ap);
	if (r && i->line) {
		char buf[SMALL_RESULT_BUF_SZ + 5/*"line "*/ + 1/*':'*/ + 1/*' '*/] = "line ";
		if (picolNumberToString(buf + 5, i->line, 10) == PICKLE_OK) {
			const size_t bl = picolStrlen(buf);
			buf[bl + 0] = ':';
			buf[bl + 1] = ' ';
			char *rn = picolAStrCat(i, buf, r);
			picolFree(i, r);
			r = rn;
		}
	}
	(void)(r ? picolForceResult(i, r, 0) : pickle_set_result_string(i, "Invalid vsnprintf"));
	return post(i, PICKLE_ERROR);
}

int pickle_set_result(pickle_t *i, const char *fmt, ...) {
	pre(i);
	assert(fmt);
	va_list ap;
	va_start(ap, fmt);
	char *r = picolVsprintf(i, fmt, ap);
	va_end(ap);
	if (!r) {
		(void)pickle_set_result_string(i, "Invalid vsnprintf");
		return post(i, PICKLE_ERROR);
	}
	return post(i, picolForceResult(i, r, 0));
}

int pickle_set_result_integer(pickle_t *i, const long result) {
	pre(i);
	return post(i, picolSetResultNumber(i, result));
}

int pickle_set_var_integer(pickle_t *i, const char *name, const long r) {
	pre(i);
	assert(name);
	return post(i, picolSetVarInteger(i, name, r));
}

int pickle_eval(pickle_t *i, const char *t) {
	pre(i);
	assert(t);
	i->line = 1;
	i->ch   = t;
	return picolEval(i, t); /* may return any int */
}

/* Arity error messages could be improved by allowing a string to describe the allowed arguments */
int pickle_set_result_error_arity(pickle_t *i, const int expected, const int argc, char **argv) {
	pre(i);
	assert(argc >= 1);
	assert(argv);
	char *as = concatenate(i, " ", argc, argv, 1, 0);
	if (!as)
		return post(i, PICKLE_ERROR);
	const int r = pickle_set_result_error(i, "Invalid argument count for %s (expected %d)\nGot: %s", argv[0], expected - 1, as);
	picolFree(i, as);
	return post(i, r);
}

int pickle_rename_command(pickle_t *i, const char *src, const char *dst) {
	pre(i);
	assert(src);
	assert(dst);
	if (picolGetCommand(i, dst))
		return post(i, pickle_set_result_error(i, "Invalid redefinition %s", dst));
	if (!compare(dst, string_empty))
		return post(i, picolUnsetCommand(i, src));
	pickle_command_t *np = picolGetCommand(i, src);
	if (!np)
		return post(i, pickle_set_result_error(i, "Invalid proc %s", src));
	int r = PICKLE_ERROR;
	if (picolIsDefinedProc(np->func)) {
		char **procdata = (char**)np->privdata;
		r = picolCommandAddProc(i, dst, procdata[0], procdata[1], np->func == picolCommandCallVariadic);
	} else {
		r = pickle_register_command(i, dst, np->func, np->privdata);
	}
	if (r != PICKLE_OK)
		return post(i, r);
	return post(i, picolUnsetCommand(i, src));
}

int pickle_set_argv(pickle_t *i, int argc, char **argv) {
	pre(i);
	assert(argc >= 0);
	implies(argc >= 0, argv);
	char *args = concatenate(i, " ", argc, argv, 1, 0);
	if (!args)
		return post(i, PICKLE_ERROR);
	const int r = pickle_set_var_string(i, "argv", args);
	if (picolFree(i, args) != PICKLE_OK)
		return post(i, PICKLE_ERROR);
	return post(i, r);
}

int pickle_concatenate(pickle_t *i, int argc, char **argv, char **cat) {
	assert(argc >= 0);
	implies(argc > 0, argv);
	assert(cat);
	pre(i);
	char *c = concatenate(i, " ", argc, argv, 1, 0);
	*cat = c;
	return post(i, c ? PICKLE_OK : PICKLE_ERROR);
}

int pickle_allocate(pickle_t *i, void **v, const size_t size) {
	assert(v);
	pre(i);
	void *vp = picolMalloc(i, size);
	if (vp) {
		zero(vp, size);
		return post(i, PICKLE_OK);
	}
	return post(i, PICKLE_ERROR);
}

int pickle_free(pickle_t *i, void **v) {
	assert(v);
	pre(i);
	void *vp = *v;
	*v = NULL;
	return post(i, picolFree(i, vp));
}

int pickle_new(pickle_t **i, const pickle_allocator_t *a) {
	assert(i);
	*i = NULL;
	const pickle_allocator_t *m = a;
	/* NB. This default allocator should probably be removed, along with
	 * the header "<stdlib.h>". */
	if (DEFAULT_ALLOCATOR) {
		static const pickle_allocator_t default_allocator = {
			.malloc  = pmalloc,
			.realloc = prealloc,
			.free    = pfree,
			.arena   = NULL,
		};
		m = a ? a : &default_allocator;
	}
	if (!m)
		return PICKLE_ERROR;
	/*implies(CONFIG_DEFAULT_ALLOCATOR == 0, m != NULL);*/
	*i = m->malloc(m->arena, sizeof(**i));
	if (!*i)
		return PICKLE_ERROR;
	const int r = picolInitialize(*i, m);
	return r == PICKLE_OK ? post(*i, r) : PICKLE_ERROR;
}

int pickle_delete(pickle_t *i) {
	if (!i)
		return PICKLE_ERROR;
	/*assert(i->initialized);*/
	const pickle_allocator_t a = i->allocator;
	const int r = picolDeinitialize(i);
	a.free(a.arena, i);
	return r != PICKLE_OK ? r : PICKLE_OK;
}

int pickle_tests(void) {
	if (!DEFINE_TESTS)
		return PICKLE_OK;
	typedef int (*test_func)(void);
	static const test_func ts[] = {
		picolTestSmallString,
		picolTestUnescape,
		picolTestConvertNumber,
		picolTestConcat,
		picolTestEval,
		picolTestGetSetVar,
		picolTestLineNumber,
		picolTestParser,
		picolTestRegex,
	};
	int r = 0;
	for (size_t i = 0; i < sizeof(ts)/sizeof(ts[0]); i++)
		if (ts[i]() != 0)
			r = -1;
	return r != 0 ? PICKLE_ERROR : PICKLE_OK;
}

