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
 * Style/coding guide and notes:
 *   - 'pickle_' and snake_case is used for exported functions/variables/types
 *   - 'picol'  and camelCase  is used for internal functions/variables/types,
 *   with a few exceptions, such as 'advance' and 'compare', which are internal
 *   functions whose names are deliberately kept short.
 *   - Use asserts wherever you can for as many preconditions, postconditions
 *   and invariants that you can think of. 
 *   - Make sure you make your functions static unless they are meant to
 *   be exported. You can use 'objdump -t | awk '$2 ~ /[Gg]/' to find
 *   all global functions. 'objdump -t | grep '\*UND\*' can be used to
 *   check that you are not pulling in functions from the C library you
 *   do not intend as well.
 *   - The core project is written strictly in C99 and uses only things
 *   that can be found in the standard library, and only things that are
 *   easy to implement on a microcontroller.
 *
 * NOTE: The callbacks all have their 'argv' argument defined as 'char **',
 * as they do not modify their arguments. However adding this in just adds
 * a lot of noise to the function definitions. Also see
 * <http://c-faq.com/ansi/constmismatch.html>.
 * TODO: Make sure the minimal set of error message strings are used throughout
 * this program. 'strip libpickle.a; strings libpickle.a;' can be used for this.
 * TODO: Remove PICKLE_MAX_STRING from the header.
 * TODO: String substitute, perhaps a 'tr' or 'sed' function would be useful as well.
 * TODO: Add list manipulation functions; 'split', 'append', 'lappend', 'lset',
 * 'lsort', 'linsert', 'lsearch', 'list', and perhaps 'foreach'. There are only 
 * a few other functions and features that can and should be added, along with these 
 * list functions, before the interpreter can be considered complete. Other
 * functions for applying a list to a function as arguments and map/reduce
 * would be useful too. These should be made to be optional. */

#include "pickle.h"
#include <assert.h>  /* !defined(NDEBUG): assert */
#include <ctype.h>   /* toupper, tolower, isalnum, isalpha, ... */
#include <stdint.h>  /* intptr_t */
#include <limits.h>  /* CHAR_BIT */
#include <stdarg.h>  /* va_list, va_start, va_end */
#include <stdio.h>   /* vsnprintf */
#include <stdlib.h>  /* !defined(DEFAULT_ALLOCATOR): free, malloc, realloc */
#include <string.h>  /* memcpy, memset, memchr, strstr, strcmp, strncmp, strcpy, strlen, strchr */

#define SMALL_RESULT_BUF_SZ       (96)
#define PRINT_NUMBER_BUF_SZ       (64 /* base 2 */ + 1 /* '-'/'+' */ + 1 /* NUL */)
#define VERSION                   (1989)
#define UNUSED(X)                 ((void)(X))
#define STRICT_NUMERIC_CONVERSION (1)
#define BUILD_BUG_ON(condition)   ((void)sizeof(char[1 - 2*!!(condition)]))
#define implies(P, Q)             assert(!(P) || (Q)) /* material implication, immaterial if NDEBUG defined */
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

#ifndef DEFAULT_ALLOCATOR
#define DEFAULT_ALLOCATOR (1)
#endif

#ifndef USE_MAX_STRING
#define USE_MAX_STRING    (0)
#endif

enum { PT_ESC, PT_STR, PT_CMD, PT_VAR, PT_SEP, PT_EOL, PT_EOF };

typedef struct { 
	unsigned nocommands :1, 
		 noescape :1, 
		 novars :1, 
		 noeval: 1;  
} pickle_parser_opts_t;

typedef PREPACK struct {
	const char *text;    /**< the program */
	const char *p;       /**< current text position */
	const char *start;   /**< token start */
	const char *end;     /**< token end */
	int *line;           /**< pointer to line number */
	const char **ch;     /**< pointer to global test position */
	int len;             /**< remaining length */
	int type;            /**< token type, PT_... */
	int insidequote;     /**< true if inside " " */
	pickle_parser_opts_t o; /**< parser options */
} POSTPACK pickle_parser_t ; /**< Parsing structure */

typedef PREPACK struct {
	char buf[SMALL_RESULT_BUF_SZ]; /**< small temporary buffer */
	char *p;                       /**< either points to buf or is allocated */
	size_t length;                 /**< length of 'p' */ 
} POSTPACK pickle_stack_or_heap_t;     /**< allocate on stack, or move to heap, depending on needs */

typedef PREPACK struct {
	const char *name;             /**< Name of function/TCL command */
	pickle_command_func_t func;   /**< Callback that actually does stuff */
	void *data;                   /**< Optional data for this function */
} POSTPACK pickle_register_command_t; /**< A single TCL command */

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
	/* NOTES:
	 * - On a 32 machine type, two bits could merged into the lowest bits
	 *   on the 'next' pointer, as these pointers are most likely aligned
	 *   on a 4 byte boundary, leaving the lowest bits free. However, this
	 *   would be non-portable. There is nothing to be gained from this,
	 *   as we have one bit left over.
	 * - On a 64 bit machine, all three bits could be merged with a
	 *   pointer, saving space in this structure. 
	 * - As we have left over bits, we could use them to mark the
	 *   variable as being one to trace, with actions for (r)ead,
	 *   (w)rite, and (u)nset. There are no bits left over in the
	 *   pickle_command struct however. The function that handled the
	 *   action would have to be global however as there is no space
	 *   to store a pointer to a variable specific handler. */
	unsigned type      : 2; /* type of data; string (pointer/small), or link (NB. Could add number type) */
	unsigned smallname : 1; /* if true, name is stored as small string */
} POSTPACK;

PREPACK struct pickle_command {
	/* If online help in the form of help strings were to be added, we
	 * could add another field for it here. */
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
	unsigned initialized   :1;           /**< if true, interpreter is initialized and ready to use */
	unsigned static_result :1;           /**< internal use only: if true, result should not be freed */
} POSTPACK;

static char        string_empty[]     = "";              /* Space saving measure */
static const char  string_oom[]       = "Out Of Memory"; /* Cannot allocate this, obviously */
static const char *string_white_space = " \t\n\r\v";
static const char *string_digits      = "0123456789abcdefghijklmnopqrstuvwxyz";

static inline void static_assertions(void) { /* A neat place to put these */
	BUILD_BUG_ON(PICKLE_MAX_STRING    < 128);
	BUILD_BUG_ON(sizeof (pickle_t) > PICKLE_MAX_STRING);
	BUILD_BUG_ON(sizeof (string_oom) > SMALL_RESULT_BUF_SZ);
	BUILD_BUG_ON(PICKLE_MAX_RECURSION < 8);
	BUILD_BUG_ON(PICKLE_MAX_ARGS      < 8);
	BUILD_BUG_ON(PICKLE_OK    !=  0);
	BUILD_BUG_ON(PICKLE_ERROR != -1);
}

static inline void *picolMalloc(pickle_t *i, size_t size) {
	assert(i);
	assert(size > 0); /* we should not allocate any zero length objects here */
	if (USE_MAX_STRING && size > PICKLE_MAX_STRING)
		return NULL;
	return i->allocator.malloc(i->allocator.arena, size);
}

static inline void *picolRealloc(pickle_t *i, void *p, size_t size) {
	assert(i);
	if (USE_MAX_STRING && size > PICKLE_MAX_STRING)
		return NULL;
	return i->allocator.realloc(i->allocator.arena, p, size);
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

static inline int picolIsBaseValid(const int base) {
	return base >= 2 && base <= 36; /* Base '0' is not a special case */
}

static inline int picolDigit(const int digit) {
	const char *found = strchr(string_digits, tolower(digit));
	return found ? (int)(found - string_digits) : -1;
}

static inline int picolIsDigit(const int digit, const int base) {
	assert(picolIsBaseValid(base));
	const int r = picolDigit(digit);
	return r < base ? r : -1;
}

static int picolConvertBaseNLong(pickle_t *i, const char *s, long *out, int base) {
	assert(i);
	assert(i->initialized);
	assert(s);
	assert(picolIsBaseValid(base));
	static const size_t max = MIN(PRINT_NUMBER_BUF_SZ, PICKLE_MAX_STRING);
	long result = 0;
	int ch = s[0]; 
	const int negate = ch == '-';
	const int prefix = negate || s[0] == '+';
	*out = 0;
	if (STRICT_NUMERIC_CONVERSION && prefix && !s[prefix])
		return pickle_set_result_error(i, "Invalid number %s", s);
	if (STRICT_NUMERIC_CONVERSION && !ch)
		return pickle_set_result_error(i, "Invalid number %s", s);
	for (size_t j = prefix; j < max && (ch = s[j]); j++) {
		const long digit = picolIsDigit(ch, base);
		if (digit < 0)
			break;
		result = digit + (result * (long)base);
	}
	if (STRICT_NUMERIC_CONVERSION && ch)
		return pickle_set_result_error(i, "Invalid number %s", s);
	if (negate)
		result = -result;
	*out = result;
	return PICKLE_OK;
}

static int picolConvertLong(pickle_t *i, const char *s, long *out) {
	assert(i);
	assert(s);
	assert(out);
	return picolConvertBaseNLong(i, s, out, 10);
}

static inline int picolCompareCaseInsensitive(const char *a, const char *b) {
	assert(a);
	assert(b);
	const size_t al = picolStrlen(a);
	const size_t bl = picolStrlen(b);
	if (a == b)
		return 0;
	if (al > bl)
		return 1;
	if (al < bl)
		return -1;
	for (size_t i = 0; i < al; i++) {
		const int ach = tolower(a[i]);
		const int bch = tolower(b[i]);
		const int diff = ach - bch;
		if (diff)
			return diff;
	}
	return 0;
}

static inline int picolLogarithm(long a, const long b, long *c) {
	assert(c);
	long r = -1;
	*c = r;
	if (a <= 0 || b < 2)
		return PICKLE_ERROR;
	do r++; while (a /= b);
	*c = r;
	return PICKLE_OK;
}

static int picolPower(long base, long exp, long *r) {
	assert(r);
	long result = 1, negative = 1;
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

/**This is may seem like an odd function, say for small allocation we want to
 * keep them on the stack, but move them to the heap when they get too big, we can use
 * the picolStackOrHeapAlloc/picolStackOrHeapFree functions to manage this.
 * @param i, instances of the pickle interpreter.
 * @param s, a small buffer, pointers and some length.
 * @param needed, number of bytes that are needed.
 * @return PICKLE_OK on success, PICKLE_ERROR on failure. */
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
		picolFree(i, s->p);
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

static char *picolStrdup(pickle_t *i, const char *s) {
	assert(i);
	assert(s);
	const size_t l = picolStrlen(s);
	char *r = picolMalloc(i, l + 1);
	return r ? memcpy(r, s, l + 1) : r;
}

static inline unsigned long picolHashString(const char *s) { /* DJB2 Hash, <http://www.cse.yorku.ca/~oz/hash.html> */
	assert(s);
	unsigned long h = 5381, ch = 0;
	for (size_t i = 0; (ch = s[i]); i++)
		h = ((h << 5) + h) + ch;
	return h;
}

static inline struct pickle_command *picolGetCommand(pickle_t *i, const char *s) {
	assert(s);
	assert(i);
	struct pickle_command *np = NULL;
	for (np = i->table[picolHashString(s) % i->length]; np != NULL; np = np->next)
		if (!compare(s, np->name))
			return np; /* found */
	return NULL; /* not found */
}

static int picolFreeResult(pickle_t *i) {
	assert(i);
	int r = PICKLE_OK;
	if (!(i->static_result))
		r = picolFree(i, (char*)i->result); /* !! */
	return r;
}

static int picolForceResult(pickle_t *i, const char *result, const int is_static) {
	assert(i);
	assert(result);
	int r = picolFreeResult(i);
	i->static_result = is_static;
	i->result = result;
	return r;
}

static int picolSetResultErrorOutOfMemory(pickle_t *i) { /* does not allocate */
	assert(i);
	(void)picolForceResult(i, string_oom, 1);
	return PICKLE_ERROR;
}

static int picolSetResultEmpty(pickle_t *i) {
	assert(i);
	return picolForceResult(i, string_empty, 1);
}

/* <https://stackoverflow.com/questions/4384359/> */
static int picolRegisterCommand(pickle_t *i, const char *name, pickle_command_func_t func, void *privdata) {
	assert(i);
	assert(name);
	assert(func);
	struct pickle_command *np = picolGetCommand(i, name);
	if (np)
		return pickle_set_result_error(i, "Invalid redefinition %s", name);
	np = picolMalloc(i, sizeof(*np));
	if (np == NULL || (np->name = picolStrdup(i, name)) == NULL) {
		picolFree(i, np);
		return picolSetResultErrorOutOfMemory(i);
	}
	const unsigned long hashval = picolHashString(name) % i->length;
	np->next = i->table[hashval];
	i->table[hashval] = np;
	np->func = func;
	np->privdata = privdata;
	return PICKLE_OK;
}

int pickle_register_command(pickle_t *i, const char *name, pickle_command_func_t func, void *privdata) {
	assert(i);
	assert(i->initialized);
	assert(name);
	assert(func);
	return picolRegisterCommand(i, name, func, privdata);
}

static void picolFreeCmd(pickle_t *i, struct pickle_command *p);

static int picolUnsetCommand(pickle_t *i, const char *name) {
	assert(i);
	assert(name);
	struct pickle_command **p = &i->table[picolHashString(name) % i->length];
	struct pickle_command *c = *p;
	for (; c; c = c->next) {
		if (!compare(c->name, name)) {
			*p = c->next;
			picolFreeCmd(i, c);
			return PICKLE_OK;
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
	memset(p, 0, sizeof *p);
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
	p->end  = p->p - 1;
	p->type = PT_CMD;
	if (*p->p == ']')
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
		p->insidequote = 1;
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
			if (!p->insidequote) {
				p->end  = p->p - 1;
				p->type = PT_ESC;
				return PICKLE_OK;
			}
			break;
		case '"':
			if (p->insidequote) {
				p->end  = p->p - 1;
				p->type = PT_ESC;
				p->insidequote = 0;
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
			if (p->insidequote)
				return picolParseString(p);
			return picolParseSep(p);
		case '\r': case '\n': case ';':
			if (p->insidequote)
				return picolParseString(p);
			return picolParseEol(p);
		case '[': {
			const int r = picolParseCommand(p);
			p->type = p->o.nocommands ? PT_STR : p->type;
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

int pickle_set_result_string(pickle_t *i, const char *s) {
	assert(i);
	assert(i->result);
	assert(i->initialized);
	assert(s);
	const size_t sl = picolStrlen(s) + 1;
	if (sl <= sizeof(i->result_buf)) {
		memcpy(i->result_buf, s, sl);
		i->static_result = 1;
		i->result = i->result_buf;
		return PICKLE_OK;
	}
	char *r = picolMalloc(i, sl);
	if (r) {
		memcpy(r, s, sl);
		picolFreeResult(i);
		i->static_result = 0;
		i->result = r;
		return PICKLE_OK;
	}
	return picolSetResultErrorOutOfMemory(i);
}

int pickle_get_result_string(pickle_t *i, const char **s) {
	assert(i);
	assert(s);
	assert(i->result);
	assert(i->initialized);
	*s = i->result;
	return PICKLE_OK;
}

int pickle_get_result_integer(pickle_t *i, long *val) {
	assert(val);
	return picolConvertLong(i, i->result, val);
}

static struct pickle_var *picolGetVar(pickle_t *i, const char *name, int link) {
	assert(i);
	assert(name);
	struct pickle_var *v = i->callframe->vars;
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

static void picolFreeVarName(pickle_t *i, struct pickle_var *v) {
	assert(i);
	assert(v);
	if (!(v->smallname))
		picolFree(i, v->name.ptr);
}

static void picolFreeVarVal(pickle_t *i, struct pickle_var *v) {
	assert(i);
	assert(v);
	if (v->type == PV_STRING)
		picolFree(i, v->data.val.ptr);
}

/* return: non-zero if and only if val fits in a small string */
static inline int picolIsSmallString(const char *val) {
	assert(val);
	/* Instead of 'memchr' we could use a bit twiddling hack to determine if
	 * there is a NUL byte in this word. See the following for more info:
	 * <https://graphics.stanford.edu/~seander/bithacks.html#ZeroInWord> */
	return !!memchr(val, 0, member_size(compact_string_t, small));
}

static int picolSetVarString(pickle_t *i, struct pickle_var *v, const char *val) {
	assert(i);
	assert(v);
	assert(val);
	if (picolIsSmallString(val)) {
		v->type = PV_SMALL_STRING;
		strcpy(v->data.val.small, val);
		return PICKLE_OK;
	}
	v->type = PV_STRING;
	return (v->data.val.ptr = picolStrdup(i, val)) ? PICKLE_OK : PICKLE_ERROR;
}

static inline int picolSetVarName(pickle_t *i, struct pickle_var *v, const char *name) {
	assert(i);
	assert(v);
	assert(name);
	if (picolIsSmallString(name)) {
		v->smallname = 1;
		strcpy(v->name.small, name);
		return PICKLE_OK;
	}
	v->smallname = 0;
	return (v->name.ptr = picolStrdup(i, name)) ? PICKLE_OK : PICKLE_ERROR;
}

int pickle_set_var_string(pickle_t *i, const char *name, const char *val) {
	assert(i);
	assert(i->initialized);
	assert(name);
	assert(val);
	struct pickle_var *v = picolGetVar(i, name, 1);
	if (v) {
		picolFreeVarVal(i, v);
		if (picolSetVarString(i, v, val) != PICKLE_OK)
			return picolSetResultErrorOutOfMemory(i);
	} else {
		if (!(v = picolMalloc(i, sizeof(*v))))
			return picolSetResultErrorOutOfMemory(i);
		const int r1 = picolSetVarName(i, v, name);
		const int r2 = picolSetVarString(i, v, val);
		if (r1 != PICKLE_OK || r2 != PICKLE_OK) {
			picolFreeVarName(i, v);
			picolFreeVarVal(i, v);
			picolFree(i, v);
			return picolSetResultErrorOutOfMemory(i);
		}
		v->next = i->callframe->vars;
		i->callframe->vars = v;
	}
	return PICKLE_OK;
}

static const char *picolGetVarVal(struct pickle_var *v) {
	assert(v);
	assert((v->type == PV_SMALL_STRING) || (v->type == PV_STRING));
	switch (v->type) {
	case PV_SMALL_STRING: return v->data.val.small;
	case PV_STRING:       return v->data.val.ptr;
	}
	return NULL;
}

int pickle_get_var_string(pickle_t *i, const char *name, const char **val) {
	assert(i);
	assert(i->initialized);
	assert(name);
	assert(val);
	*val = NULL;
	struct pickle_var *v = picolGetVar(i, name, 1);
	if (!v)
		return PICKLE_ERROR;
	*val = picolGetVarVal(v);
	return *val ? PICKLE_OK : PICKLE_ERROR;
}

int pickle_get_var_integer(pickle_t *i, const char *name, long *val) {
	assert(i);
	assert(i->initialized);
	assert(name);
	assert(val);
	*val = 0;
	const char *s = NULL;
	const int retcode = pickle_get_var_string(i, name, &s);
	if (!s || retcode != PICKLE_OK)
		return PICKLE_ERROR;
	return picolConvertLong(i, s, val);
}

static inline void swap(char * const a, char * const b) {
	assert(a);
	assert(b);
	const char t = *a;
	*a = *b;
	*b = t;
}

static inline char *reverse(char *s, size_t length) { /* Modifies Argument */
	assert(s);
	for (size_t i = 0; i < (length/2); i++)
		swap(&s[i], &s[(length - i) - 1]);
	return s;
}

static int picolLongToString(char buf[static 64/*base 2*/ + 1/*'+'/'-'*/ + 1/*NUL*/], long in, int base) {
	assert(buf);
	int negate = 0;
	size_t i = 0;
	unsigned long dv = in;
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
		int r = vsnprintf(h.p, h.length, fmt, mine);
		if (r < 0)
			goto fail;
		if (r < (int)h.length) /* Casting to 'int' is not ideal, but we have no choice */
			break;
		if (picolStackOrHeapAlloc(i, &h, r) != PICKLE_OK)
			goto fail;
	}
	if (h.buf == h.p) {
		char *r = picolStrdup(i, h.buf);
		picolStackOrHeapFree(i, &h);
		return r;
	}
	return h.p;
fail:
	picolStackOrHeapFree(i, &h);
	return NULL;
}

int pickle_set_result_error(pickle_t *i, const char *fmt, ...) {
	assert(i);
	assert(i->initialized);
	assert(fmt);
	va_list ap;
	va_start(ap, fmt);
	char *r = picolVsprintf(i, fmt, ap);
	va_end(ap);
	if (r && i->line) {
		/* TODO: Add line number */
	}
	(void)(r ? picolForceResult(i, r, 0) : pickle_set_result_string(i, "Invalid vsnprintf"));
	return PICKLE_ERROR;
}

int pickle_set_result(pickle_t *i, const char *fmt, ...) {
	assert(i);
	assert(i->initialized);
	assert(fmt);
	va_list ap;
	va_start(ap, fmt);
	char *r = picolVsprintf(i, fmt, ap);
	va_end(ap);
	if (!r) {
		(void)pickle_set_result_string(i, "Invalid vsnprintf");
		return PICKLE_ERROR;
	}
	return picolForceResult(i, r, 0);
}

int pickle_set_result_integer(pickle_t *i, const long result) {
	assert(i);
	assert(i->initialized);
	char buffy/*<3*/[PRINT_NUMBER_BUF_SZ] = { 0 };
	if (picolLongToString(buffy, result, 10) != PICKLE_OK)
		return pickle_set_result_error(i, "Invalid conversion");
	return pickle_set_result_string(i, buffy);
}

int pickle_set_var_integer(pickle_t *i, const char *name, const long r) {
	assert(i);
	assert(i->initialized);
	assert(name);
	char buffy[PRINT_NUMBER_BUF_SZ] = { 0 };
	if (picolLongToString(buffy, r, 10) != PICKLE_OK)
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
		for (int i = 0; i < argc; i++)
			assert(argv[i]);
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
			case  'n': r[k] = '\n'; break;
			case  't': r[k] = '\t'; break;
			case  'r': r[k] = '\r'; break;
			case  '"': r[k] = '"';  break;
			case  '[': r[k] = '[';  break;
			case  ']': r[k] = ']';  break;
			case  'e': r[k] = 27;   break;
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
				return -3;
			}
		} else {
			r[k] = ch;
		}
	}
	r[k] = 0;
	return k;
}

/* TODO: Add quoted string evaluation as an option */
static char *concatenate(pickle_t *i, const char *join, const int argc, char **argv) {
	assert(i);
	assert(join);
	assert(argc >= 0);
	implies(argc > 0, argv != NULL);
	if (argc == 0)
		return picolStrdup(i, "");
	if (argc > (int)PICKLE_MAX_ARGS)
		return NULL;
	const size_t jl = picolStrlen(join);
	size_t ls[PICKLE_MAX_ARGS], l = 0;
	for (int j = 0; j < argc; j++) {
		const size_t sz = picolStrlen(argv[j]);
		ls[j] = sz;
		l += sz + jl;
	}
	if (USE_MAX_STRING && ((l + 1) >= PICKLE_MAX_STRING))
		return NULL;
	pickle_stack_or_heap_t h = { .p = NULL };
	if (picolStackOrHeapAlloc(i, &h, l) != PICKLE_OK)
		return NULL;
	l = 0;
	for (int j = 0; j < argc; j++) {
		implies(USE_MAX_STRING, l < PICKLE_MAX_STRING);
		memcpy(h.p + l, argv[j], ls[j]);
		l += ls[j];
		if (jl && (j + 1) < argc) {
			implies(USE_MAX_STRING, l < PICKLE_MAX_STRING);
			memcpy(h.p + l, join, jl);
			l += jl;
		}
	}
	h.p[l] = '\0';
	if (h.p != h.buf)
		return h.p;
	char *str = picolStrdup(i, h.p);
	picolStackOrHeapFree(i, &h);
	return str;
}

static inline int picolDoCommand(pickle_t *i, int argc, char *argv[]) {
	assert(i);
	assert(argc >= 1);
	assert(argv);
	struct pickle_command *c = picolGetCommand(i, argv[0]);
	if (c == NULL) { 
		if ((c = picolGetCommand(i, "unknown")) == NULL) 
			return pickle_set_result_error(i, "Invalid command %s", argv[0]);
		/* TODO: Turn 'argv' into a proper TCL list */
		char *arg2 = concatenate(i, " ", argc, argv);
		if (!arg2)
			return picolSetResultErrorOutOfMemory(i);
		char *nargv[] = { "unknown", arg2 };
		picolAssertCommandPreConditions(i, 2, nargv, c->privdata);
		const int r = c->func(i, 2, nargv, c->privdata);
		picolAssertCommandPostConditions(i, r);
		picolFree(i, arg2);
		return r;
	} 
	picolAssertCommandPreConditions(i, argc, argv, c->privdata);
	const int r = c->func(i, argc, argv, c->privdata);
	picolAssertCommandPostConditions(i, r);
	return r;
}

static int picolEvalAndSubst(pickle_t *i, pickle_parser_opts_t *o, const char *t) {
	assert(i);
	assert(i->initialized);
	/* NB: assert(o || !o); */
	assert(t);
	pickle_parser_t p = { NULL };
	int retcode = PICKLE_OK, argc = 0;
	char **argv = NULL;
	if (picolSetResultEmpty(i) != PICKLE_OK)
		return PICKLE_ERROR;
	picolParserInitialize(&p, o, t, &i->line, &i->ch);
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
			retcode = picolSetResultErrorOutOfMemory(i);
			goto err;
		}
		memcpy(t, p.start, tlen);
		t[tlen] = '\0';
		if (p.type == PT_VAR) {
			struct pickle_var * const v = picolGetVar(i, t, 1);
			if (!v) {
				retcode = pickle_set_result_error(i, "Invalid variable %s", t);
				picolFree(i, t);
				goto err;
			}
			picolFree(i, t);
			if (!(t = picolStrdup(i, picolGetVarVal(v)))) {
				retcode = picolSetResultErrorOutOfMemory(i);
				goto err;
			}
		} else if (p.type == PT_CMD) {
			retcode = picolEvalAndSubst(i, NULL, t); // NB!
			picolFree(i, t);
			if (retcode != PICKLE_OK)
				goto err;
			if (!(t = picolStrdup(i, i->result))) {
				retcode = picolSetResultErrorOutOfMemory(i);
				goto err;
			}
		} else if (p.type == PT_ESC) {
			if (picolUnEscape(t, tlen + 1/*NUL terminator*/) < 0) {
				retcode = pickle_set_result_error(i, "Invalid escape sequence %s", t); /* BUG: %s is probably mangled by now */
				picolFree(i, t);
				goto err;
			}
		} else if (p.type == PT_SEP) {
			prevtype = p.type;
			picolFree(i, t);
			continue;
		}

		if (p.type == PT_EOL) { /* We have a complete command + args. Call it! */
			picolFree(i, t);
			prevtype = p.type;
			if (p.o.noeval) {
				char *result = concatenate(i, " ", argc, argv);
				if (!result) {
					retcode = picolSetResultErrorOutOfMemory(i);
					goto err;
				}
				if ((retcode = pickle_set_result_string(i, result)) != PICKLE_OK)
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
				retcode = picolSetResultErrorOutOfMemory(i);
				goto err;
			}
			argv[argc] = t;
			t = NULL;
			argc++;
		} else { /* Interpolation */
			const int oldlen = picolStrlen(argv[argc - 1]), tlen = picolStrlen(t);
			char *arg = picolRealloc(i, argv[argc - 1], oldlen + tlen + 1);
			if (!arg) {
				retcode = picolSetResultErrorOutOfMemory(i);
				picolFree(i, t);
				goto err;
			}
			argv[argc - 1] = arg;
			memcpy(argv[argc - 1] + oldlen, t, tlen);
			argv[argc - 1][oldlen + tlen] = '\0';
		}
		picolFree(i, t);
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

int pickle_eval(pickle_t *i, const char *t) {
	assert(i);
	assert(i->initialized);
	assert(t);
	i->line = 1;
	i->ch   = t;
	return picolEval(i, t);
}

int pickle_set_result_error_arity(pickle_t *i, const int expected, const int argc, char **argv) {
	assert(i);
	assert(i->initialized);
	assert(argc >= 1);
	assert(argv);
	char *as = concatenate(i, " ", argc, argv);
	if (!as)
		return picolSetResultErrorOutOfMemory(i);
	const int r = pickle_set_result_error(i, "Invalid argument count for %s (expected %d)\nGot: %s", argv[0], expected - 1, as);
	picolFree(i, as);
	return r;
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

static inline const char *trimleft(const char *class, const char *s) { /* Returns pointer to s */
	assert(class);
	assert(s);
	size_t j = 0, k = 0;
	while (s[j] && strchr(class, s[j++]))
		k = j;
	return &s[k];
}

static inline void trimright(const char *class, char *s) { /* Modifies argument */
	assert(class);
	assert(s);
	const size_t length = picolStrlen(s);
	size_t j = length - 1;
	if (j > length)
		return;
	while (j > 0 && strchr(class, s[j]))
		j--;
	if (s[j])
		s[j + !strchr(class, s[j])] = 0;
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

static inline int picolCommandString(pickle_t *i, const int argc, char **argv, void *pd) { /* Big! */
	UNUSED(pd);
	assert(!pd);
	if (argc < 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	int r = PICKLE_OK;
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
			memcpy(h.p, arg1, l + 1);
			trimright(string_white_space, h.p);
			r = pickle_set_result_string(i, h.p);
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!compare(rq, "trim"))      {
			const size_t l = picolStrlen(arg1);
			if (picolStackOrHeapAlloc(i, &h, l + 1) != PICKLE_OK)
				return -1;
			memcpy(h.p, arg1, l + 1);
			trimright(string_white_space, h.p);
			r = pickle_set_result_string(i, trimleft(string_white_space, h.p));
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!compare(rq, "length"))
			return pickle_set_result_integer(i, picolStrlen(arg1));
		if (!compare(rq, "toupper")) {
			if (picolStackOrHeapAlloc(i, &h, picolStrlen(arg1) + 1) != PICKLE_OK)
				return PICKLE_ERROR;
			size_t j = 0;
			for (j = 0; arg1[j]; j++)
				h.p[j] = toupper(arg1[j]);
			h.p[j] = 0;
			if (h.p != h.buf)
				return picolForceResult(i, h.p, 0);
			r = pickle_set_result_string(i, h.p);
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
			if (h.p != h.buf)
				return picolForceResult(i, h.p, 0);
			r = pickle_set_result_string(i, h.p);
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
			memcpy(h.p, arg1, l + 1);
			r = pickle_set_result_string(i, reverse(h.p, l));
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!compare(rq, "ordinal"))
			return pickle_set_result_integer(i, arg1[0]);
		if (!compare(rq, "char")) {
			long v = 0;
			if (picolConvertLong(i, arg1, &v) != PICKLE_OK)
				return PICKLE_ERROR;
			char b[] = { v, 0 };
			return pickle_set_result_string(i, b);
		}
		if (!compare(rq, "dec2hex")) {
			long hx = 0;
			if (picolConvertLong(i, arg1, &hx) != PICKLE_OK)
				return PICKLE_ERROR;
			BUILD_BUG_ON(SMALL_RESULT_BUF_SZ < PRINT_NUMBER_BUF_SZ);
			if (picolLongToString(h.buf, hx, 16) != PICKLE_OK)
				return pickle_set_result_error(i, "Invalid conversion %s", h.buf);
			return pickle_set_result_string(i, h.buf);
		}
		if (!compare(rq, "hex2dec")) {
			long l = 0;
			if (picolConvertBaseNLong(i, arg1, &l, 16) != PICKLE_OK)
				return pickle_set_result_error(i, "Invalid conversion %s", arg1);
			return pickle_set_result_integer(i, l);
		}
		if (!compare(rq, "hash"))
			return pickle_set_result_integer(i, picolHashString(arg1));
	} else if (argc == 4) {
		const char *arg1 = argv[2], *arg2 = argv[3];
		if (!compare(rq, "trimleft"))
			return pickle_set_result_string(i, trimleft(arg2, arg1));
		if (!compare(rq, "trimright")) {
			const size_t l = picolStrlen(arg1);
			if (picolStackOrHeapAlloc(i, &h, l + 1) != PICKLE_OK)
				return PICKLE_ERROR;
			memcpy(h.p, arg1, l + 1);
			trimright(arg2, h.p);
			r = pickle_set_result_string(i, h.p);
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!compare(rq, "trim"))   {
			const size_t l = picolStrlen(arg1);
			if (picolStackOrHeapAlloc(i, &h, l + 1) != PICKLE_OK)
				return PICKLE_ERROR;
			memcpy(h.p, arg1, l + 1);
			trimright(arg2, h.p);
			r = pickle_set_result_string(i, trimleft(arg2, h.p));
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!compare(rq, "match"))  {
			const int r = match(arg1, arg2, PICKLE_MAX_RECURSION - i->level);
			if (r < 0)
				return pickle_set_result_error(i, "Invalid regex %d", r);
			return pickle_set_result_integer(i, r);
		}
		if (!compare(rq, "equal"))
			return pickle_set_result_integer(i, !compare(arg1, arg2));
		if (!compare(rq, "compare"))
			return pickle_set_result_integer(i, compare(arg1, arg2));
		if (!compare(rq, "compare-no-case"))
			return pickle_set_result_integer(i, picolCompareCaseInsensitive(arg1, arg2));
		if (!compare(rq, "index"))   {
			long index = 0;
			if (picolConvertLong(i, arg2, &index) != PICKLE_OK)
				return PICKLE_ERROR;
			const long length = picolStrlen(arg1);
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
			if (!compare(arg1, "alnum"))    { while (isalnum(*arg2))  arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "alpha"))    { while (isalpha(*arg2))  arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "digit"))    { while (isdigit(*arg2))  arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "graph"))    { while (isgraph(*arg2))  arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "lower"))    { while (islower(*arg2))  arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "print"))    { while (isprint(*arg2))  arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "punct"))    { while (ispunct(*arg2))  arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "space"))    { while (isspace(*arg2))  arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "upper"))    { while (isupper(*arg2))  arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "xdigit"))   { while (isxdigit(*arg2)) arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "ascii"))    { while (*arg2 && !(0x80 & *arg2)) arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "control"))  { while (*arg2 && iscntrl(*arg2))  arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "wordchar")) { while (isalnum(*arg2) || *arg2 == '_')  arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "false"))    { return pickle_set_result_integer(i, isFalse(arg2)); }
			if (!compare(arg1, "true"))     { return pickle_set_result_integer(i, isTrue(arg2)); }
			if (!compare(arg1, "boolean"))  { return pickle_set_result_integer(i, isTrue(arg2) || isFalse(arg2)); }
			if (!compare(arg1, "integer"))  { return pickle_set_result_integer(i, picolConvertLong(i, arg2, &(long){0l}) == PICKLE_OK); }
			/* Missing: double */
		}
		if (!compare(rq, "repeat")) {
			long count = 0, j = 0;
			const size_t length = picolStrlen(arg1);
			if (picolConvertLong(i, arg2, &count) != PICKLE_OK)
				return PICKLE_ERROR;
			if (count < 0)
				return pickle_set_result_error(i, "Invalid range %ld", count);
			if (picolStackOrHeapAlloc(i, &h, (count * length) + 1) != PICKLE_OK)
				return PICKLE_ERROR;
			for (; j < count; j++) {
				implies(USE_MAX_STRING, (((j * length) + length) < PICKLE_MAX_STRING));
				memcpy(&h.p[j * length], arg1, length);
			}
			h.p[j * length] = 0;
			r = pickle_set_result_string(i, h.p);
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
		if (!compare(rq, "first"))      {
			const char *found = strstr(arg2, arg1);
			if (!found)
				return pickle_set_result_integer(i, -1);
			return pickle_set_result_integer(i, found - arg2);
		}
		if (!compare(rq, "base2dec")) {
			long b = 0, n = 0;
			if (picolConvertLong(i, arg2, &b) != PICKLE_OK)
				return PICKLE_ERROR;
			if (!picolIsBaseValid(b))
				return pickle_set_result_error(i, "Invalid base %ld", b);
			if (picolConvertBaseNLong(i, arg1, &n, b) != PICKLE_OK)
				return pickle_set_result_error(i, "Invalid conversion %s", arg1);
			return pickle_set_result_integer(i, n);
		}
		if (!compare(rq, "dec2base")) {
			long b = 0, n = 0;
			if (picolConvertLong(i, arg2, &b) != PICKLE_OK)
				return PICKLE_ERROR;
			if (!picolIsBaseValid(b))
				return pickle_set_result_error(i, "Invalid base %ld", b);
			if (picolConvertLong(i, arg1, &n) != PICKLE_OK)
				return pickle_set_result_error(i, "Invalid conversion %s", arg1);
			BUILD_BUG_ON(SMALL_RESULT_BUF_SZ < PRINT_NUMBER_BUF_SZ);
			if (picolLongToString(h.buf, n, b) != PICKLE_OK)
				return pickle_set_result_error(i, "Invalid conversion %s", arg1);
			return pickle_set_result_string(i, h.buf);
		}
	} else if (argc == 5) {
		const char *arg1 = argv[2], *arg2 = argv[3], *arg3 = argv[4];
		if (!compare(rq, "first"))      {
			const long length = picolStrlen(arg2);
			long start  = 0;
			if (picolConvertLong(i, arg3, &start) != PICKLE_OK)
				return PICKLE_ERROR;
			if (start < 0 || start >= length)
				return picolSetResultEmpty(i);
			const char *found = strstr(arg2 + start, arg1);
			if (!found)
				return pickle_set_result_integer(i, -1);
			return pickle_set_result_integer(i, found - arg2);
		}
		if (!compare(rq, "range")) {
			const long length = picolStrlen(arg1);
			long first = 0, last = 0;
			if (picolConvertLong(i, arg2, &first) != PICKLE_OK)
				return PICKLE_ERROR;
			if (picolConvertLong(i, arg3, &last) != PICKLE_OK)
				return PICKLE_ERROR;
			if (first > last)
				return picolSetResultEmpty(i);
			if (first < 0)
				first = 0;
			if (last > length)
				last = length;
			const long diff = (last - first) + 1;
			if (picolStackOrHeapAlloc(i, &h, diff) != PICKLE_OK)
				return PICKLE_ERROR;
			memcpy(h.p, &arg1[first], diff);
			h.p[diff] = 0;
			r = pickle_set_result_string(i, h.p);
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
	}
	return pickle_set_result_error_arity(i, 3, argc, argv);
}

enum { UNOT, UINV, UABS, UBOOL };
enum { 
	BADD,  BSUB,    BMUL,    BDIV, BMOD,
	BMORE, BMEQ,    BLESS,   BLEQ, BEQ,
	BNEQ,  BLSHIFT, BRSHIFT, BAND, BOR,
	BXOR,  BMIN,    BMAX,    BPOW, BLOG
};

static int picolCommandMathUnary(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	long a = 0;
	if (picolConvertLong(i, argv[1], &a) != PICKLE_OK)
		return PICKLE_ERROR;
	switch ((intptr_t)(char*)pd) {
	case UNOT:  a = !a; break;
	case UINV:  a = ~a; break;
	case UABS:  a = a < 0 ? -a : a; break;
	case UBOOL: a = !!a; break;
	default: return pickle_set_result_error(i, "Invalid operator %s", argv[0]);
	}
	return pickle_set_result_integer(i, a);
}

static int picolCommandMath(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	long a = 0, b = 0;
	if (picolConvertLong(i, argv[1], &a) != PICKLE_OK)
		return PICKLE_ERROR;
	if (picolConvertLong(i, argv[2], &b) != PICKLE_OK)
		return PICKLE_ERROR;
	long c = 0;
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
	case BLSHIFT: c = ((unsigned long)a) << b; break;
	case BRSHIFT: c = ((unsigned long)a) >> b; break;
	case BAND:    c = a & b; break;
	case BOR:     c = a | b; break;
	case BXOR:    c = a ^ b; break;
	case BMIN:    c = MIN(a, b); break;
	case BMAX:    c = MAX(a, b); break;
	case BPOW:    if (picolPower(a, b, &c)     != PICKLE_OK) return pickle_set_result_error(i, "Invalid power"); break;
	case BLOG:    if (picolLogarithm(a, b, &c) != PICKLE_OK) return pickle_set_result_error(i, "Invalid logarithm"); break;
	default: return pickle_set_result_error(i, "Invalid operator %s", argv[0]);
	}
	return pickle_set_result_integer(i, c);
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
	return pickle_set_var_integer(i, argv[2], picolEval(i, argv[1]));
}

static int picolCommandIf(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	int retcode = 0;
	if (argc != 3 && argc != 5)
		return pickle_set_result_error_arity(i, 5, argc, argv);
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

static int picolCommandLIndex(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc != 3)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	pickle_parser_t p = { NULL };
	const char *parse = argv[1];
       	size_t count = 0; 
	long index = 0;
	if (picolConvertLong(i, argv[2], &index) != PICKLE_OK)
		return PICKLE_ERROR;
	picolParserInitialize(&p, NULL, parse, NULL, NULL); /** TODO: Refactor */
	for (;;) {
		if (picolGetToken(&p) != PICKLE_OK)
			return pickle_set_result_error(i, "Invalid parse");
		const int t = p.type;
		if (t == PT_EOF)
			break;
		if (t == PT_STR || t == PT_CMD || t == PT_VAR || t == PT_ESC)
			count++;
		if (count > (size_t)index) {
			pickle_stack_or_heap_t h = { .p = NULL };
			const size_t l = p.end - p.start + 1;
			implies(USE_MAX_STRING, l <= PICKLE_MAX_STRING);
			if (picolStackOrHeapAlloc(i, &h, l) != PICKLE_OK)
				return PICKLE_ERROR;
			const int r = pickle_set_result_string(i, memcpy(h.p, p.start, l));
			if (picolStackOrHeapFree(i, &h) != PICKLE_OK)
				return PICKLE_ERROR;
			return r;
		}
	}
	return picolSetResultEmpty(i);
}

static int picolCommandLLength(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	const char *parse = trimleft(string_white_space, argv[1]);
	pickle_parser_t p = { NULL };
	picolParserInitialize(&p, NULL, parse, NULL, NULL); /** TODO: Refactor */
	size_t count = 0;
	for (;;) {
		if (picolGetToken(&p) != PICKLE_OK)
			return pickle_set_result_error(i, "Invalid parse");
		if (p.type == PT_EOF)
			break;
		if (p.type != PT_SEP)
			count++;
	}
	return pickle_set_result_integer(i, count ? count - 1 : count);
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

static void picolVarFree(pickle_t *i, struct pickle_var *v) {
	if (!v)
		return;
	picolFreeVarName(i, v);
	picolFreeVarVal(i, v);
	picolFree(i, v);
}

static void picolDropCallFrame(pickle_t *i) {
	assert(i);
	struct pickle_call_frame *cf = i->callframe;
	assert(i->level >= 0);
	i->level--;
	if (!cf)
		return;
	struct pickle_var *v = cf->vars, *t = NULL;
	while (v) {
		assert(v != v->next); /* Cycle? */
		t = v->next;
		picolVarFree(i, v);
		v = t;
	}
	i->callframe = cf->parent;
	picolFree(i, cf);
}

static void picolDropAllCallFrames(pickle_t *i) {
	assert(i);
	while (i->callframe)
		picolDropCallFrame(i);
}

static int picolCommandCallProc(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (i->level > (int)PICKLE_MAX_RECURSION)
		return pickle_set_result_error(i, "Invalid recursion %d", PICKLE_MAX_RECURSION);
	char **x = pd, *alist = x[0], *body = x[1], *p = picolStrdup(i, alist), *tofree = NULL;
	int arity = 0, errcode = PICKLE_OK;
	struct pickle_call_frame *cf = picolMalloc(i, sizeof(*cf));
	if (!cf || !p) {
		picolFree(i, p);
		picolFree(i, cf);
		return picolSetResultErrorOutOfMemory(i);
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
			picolFree(i, tofree);
			picolDropCallFrame(i);
			return picolSetResultErrorOutOfMemory(i);
		}
		p++;
	}
	picolFree(i, tofree);
	if (arity != (argc - 1))
		goto arityerr;
	errcode = picolEval(i, body);
	if (errcode == PICKLE_RETURN)
		errcode = PICKLE_OK;
	picolDropCallFrame(i);
	return errcode;
arityerr:
	pickle_set_result_error(i, "Invalid argument count for %s", argv[0]);
	picolDropCallFrame(i);
	return PICKLE_ERROR;
}

/* NOTE: If space is really at a premium it would be possible to store the
 * strings compressed, decompressing them when needed. Perhaps 'smaz' library,
 * with a custom dictionary, could be used for this, see
 * <https://github.com/antirez/smaz> for more information. */
static int picolCommandAddProc(pickle_t *i, const char *name, const char *args, const char *body) {
	assert(i);
	assert(name);
	assert(args);
	assert(body);
	char **procdata = picolMalloc(i, sizeof(char*)*2);
	if (!procdata)
		return picolSetResultErrorOutOfMemory(i);
	procdata[0] = picolStrdup(i, args); /* arguments list */
	procdata[1] = picolStrdup(i, body); /* procedure body */
	if (!(procdata[0]) || !(procdata[1])) {
		picolFree(i, procdata[0]);
		picolFree(i, procdata[1]);
		picolFree(i, procdata);
		return picolSetResultErrorOutOfMemory(i);
	}
	return pickle_register_command(i, name, picolCommandCallProc, procdata);
}

static int picolCommandProc(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(!pd);
	if (argc != 4)
		return pickle_set_result_error_arity(i, 4, argc, argv);
	return picolCommandAddProc(i, argv[1], argv[2], argv[3]);
}

int pickle_rename_command(pickle_t *i, const char *src, const char *dst) {
	assert(i);
	assert(i->initialized);
	assert(src);
	assert(dst);
	if (picolGetCommand(i, dst))
		return pickle_set_result_error(i, "Invalid redefinition %s", dst);
	if (!compare(dst, string_empty))
		return picolUnsetCommand(i, src);
	struct pickle_command *np = picolGetCommand(i, src);
	if (!np)
		return pickle_set_result_error(i, "Invalid proc %s", src);
	int r = PICKLE_ERROR;
	if (np->func == picolCommandCallProc) {
		char **procdata = (char**)np->privdata;
		r = picolCommandAddProc(i, dst, procdata[0], procdata[1]);
	} else {
		r = pickle_register_command(i, dst, np->func, np->privdata);
	}
	if (r != PICKLE_OK)
		return r;
	return picolUnsetCommand(i, src);
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
	long retcode = PICKLE_RETURN;
	if (argc == 3)
		if (picolConvertLong(i, argv[2], &retcode) != PICKLE_OK)
			return PICKLE_ERROR;
	if (argc == 1)
		return picolSetResultEmpty(i) != PICKLE_OK ? PICKLE_ERROR : PICKLE_RETURN;
	if (pickle_set_result_string(i, argv[1]) != PICKLE_OK)
		return PICKLE_ERROR;
	return retcode;
}

static int doJoin(pickle_t *i, const char *join, const int argc, char **argv) {
	char *e = concatenate(i, join, argc, argv);
	if (!e)
		return picolSetResultErrorOutOfMemory(i);
	picolFreeResult(i);
	i->static_result = 0;
	i->result = e;
	return PICKLE_OK;
}

static int picolCommandConcat(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	return doJoin(i, " ", argc - 1, argv + 1);
}

static int picolCommandJoinArgs(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc < 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	return doJoin(i, argv[1], argc - 2, argv + 2);
}

static int picolCommandJoin(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	const char *parse = argv[1], *join = argv[2];
	int r = PICKLE_OK;
	size_t count = 0;
	char *arguments[PICKLE_MAX_ARGS] = { 0 };
	pickle_parser_t p = { NULL };
	picolParserInitialize(&p, NULL, parse, NULL, NULL); /* TODO: refactor with new parser? */
	for (;;) {
		if (picolGetToken(&p) == PICKLE_ERROR) {
			r = pickle_set_result_error(i, "parser error");
			goto end;
		}
		if (p.type == PT_EOF)
			break;
		if (p.type == PT_EOL)
			continue;
		if (p.type != PT_SEP) {
			if (count >= PICKLE_MAX_ARGS) {
				r = pickle_set_result_error(i, "Invalid argument count %d", count);
				goto end;
			}
			assert(p.end >= p.start);
			pickle_stack_or_heap_t h = { .p = NULL };
			size_t needed = (p.end - p.start) + 1;
			if (picolStackOrHeapAlloc(i, &h, needed + 1) != PICKLE_OK) {
				r = picolSetResultErrorOutOfMemory(i);
				goto end;
			}
			memcpy(h.p, p.start, needed);
			h.p[needed] = 0;
			if (h.p != h.buf) {
				arguments[count++] = h.p;
			} else {
				arguments[count++] = picolStrdup(i, h.p);
				picolStackOrHeapFree(i, &h);
			}
		}
	}
end:
	if (r >= 0)
		r = doJoin(i, join, count, (char **)arguments);
	for (size_t j = 0; j < count; j++)
		picolFree(i, arguments[j]);
	return r;
}

static int picolCommandEval(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	int r = doJoin(i, " ", argc - 1, argv + 1);
	if (r == PICKLE_OK) {
		char *e = picolStrdup(i, i->result);
		if (!e)
			return picolSetResultErrorOutOfMemory(i);
		r = picolEval(i, e);
		picolFree(i, e);
	}
	return r;
}

static int picolCommandSubst(pickle_t *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	pickle_parser_opts_t o = { 0, 0, 0, 1 };
again:
	if (argc < 2 || argc > 5)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	if (!strcmp(argv[1], "-nobackslashes")) {
		argc--, argv++;
		o.noescape = 1;
		goto again;
	}
	if (!strcmp(argv[1], "-novariables")) {
		argc--, argv++;
		o.novars = 1;
		goto again;
	}
	if (!strcmp(argv[1], "-nocommands")) {
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
	long level = 0;
	if (picolConvertLong(i, top ? &levelStr[1] : levelStr, &level) != PICKLE_OK)
		return PICKLE_ERROR;
	if (top)
		level = i->level - level;
	if (level < 0)
		return pickle_set_result_error(i, "Invalid level passed to 'uplevel/upvar': %d", level);

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

	struct pickle_call_frame *cf = i->callframe;
	int retcode = PICKLE_OK;
	if ((retcode = pickle_set_var_string(i, argv[3], "")) != PICKLE_OK) {
		pickle_set_result_error(i, "Invalid redefinition %s", argv[3]);
		goto end;
	}

	struct pickle_var *myVar = cf->vars, *otherVar = NULL;

	if ((retcode = picolSetLevel(i, argv[1])) != PICKLE_OK)
		goto end;
	if (!(otherVar = picolGetVar(i, argv[2], 1))) {
		if (pickle_set_var_string(i, argv[2], "") != PICKLE_OK)
			return picolSetResultErrorOutOfMemory(i);
		otherVar = i->callframe->vars;
	}

	if (myVar == otherVar) { /* more advance cycle detection should be done here */
		pickle_set_result_error(i, "Invalid circular reference %s", argv[3]);
		goto end;
	}

	myVar->type = PV_LINK;
	myVar->data.link = otherVar;

	/*while (myVar->type == PV_LINK) { // Do we need the PV_LINK Type?
		assert(myVar != myVar->data.link); // Cycle?
		myVar = myVar->data.link;
	}*/
end:
	i->callframe = cf;
	return retcode;
}

static int picolCommandUpLevel(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc < 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	struct pickle_call_frame *cf = i->callframe;
	int retcode = picolSetLevel(i, argv[1]);
	if (retcode == PICKLE_OK) {
		char *e = concatenate(i, " ", argc - 2, argv + 2);
		if (!e) {
			retcode = picolSetResultErrorOutOfMemory(i);
			goto end;
		}
		retcode = picolEval(i, e);
		picolFree(i, e);
	}
end:
	i->callframe = cf;
	return retcode;
}

static inline int picolUnsetVar(pickle_t *i, const char *name) { /* TODO: Make sure no links point to this unset variable */
	assert(i);
	assert(name);
	struct pickle_call_frame *cf = i->callframe;
	struct pickle_var *p = NULL, *deleteMe = picolGetVar(i, name, 0/*NB!*/);
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

static int picolCommandCommand(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc == 1) {
		long r = 0;
		for (long j = 0; j < i->length; j++) {
			struct pickle_command *c = i->table[j];
			for (; c; c = c->next) {
				r++;
				assert(c != c->next);
			}
		}
		return pickle_set_result_integer(i, r);
	}
	if (argc == 2) {
		long r = -1, j = 0;
		for (long k = 0; k < i->length; k++) {
			struct pickle_command *c = i->table[k];
			for (; c; c = c->next) {
				if (!compare(argv[1], c->name)) {
					r = j;
					goto done;
				}
				j++;
			}
		}
	done:
		return pickle_set_result_integer(i, r);
	}

	if (argc != 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	struct pickle_command *c = NULL;
	long r = 0, j = 0;
	if (picolConvertLong(i, argv[2], &r) != PICKLE_OK)
		return PICKLE_ERROR;
	for (long k = 0; k < i->length; k++) {
		struct pickle_command *p = i->table[k];
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
	const int defined = c->func == picolCommandCallProc;
	const char *rq = argv[1];
	if (!compare(rq, "args")) {
		if (!defined)
			return pickle_set_result(i, "{built-in %p %p}", c->func, c->privdata); 
		char **procdata = c->privdata;
		return pickle_set_result_string(i, procdata[0]);
	} else if (!compare(rq, "body")) {
		if (!defined)
			return pickle_set_result(i, "{built-in %p %p}", c->func, c->privdata); 
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
		return pickle_set_result_integer(i, i->line);
	if (!compare(rq, "level"))
		return pickle_set_result_integer(i, i->level);
	if (!compare(rq, "width"))
		return pickle_set_result_integer(i, CHAR_BIT * sizeof(char*));
	if (argc < 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	if (!compare(rq, "limits")) {
		rq = argv[2];
		if (!compare(rq, "recursion"))
			return pickle_set_result_integer(i, PICKLE_MAX_RECURSION);
		if (!compare(rq, "string"))
			return pickle_set_result_integer(i, PICKLE_MAX_STRING);
		if (!compare(rq, "arguments"))
			return pickle_set_result_integer(i, PICKLE_MAX_ARGS);
		if (!compare(rq, "minimum"))
			return pickle_set_result_integer(i, LONG_MIN);
		if (!compare(rq, "maximum"))
			return pickle_set_result_integer(i, LONG_MAX);
	}
	if (!compare(rq, "features")) {
		if (!compare(rq, "allocator"))
			return pickle_set_result_integer(i, DEFAULT_ALLOCATOR);
		if (!compare(rq, "string"))
			return pickle_set_result_integer(i, DEFINE_STRING);
		if (!compare(rq, "maths"))
			return pickle_set_result_integer(i, DEFINE_MATHS);
		if (!compare(rq, "debugging"))
			return pickle_set_result_integer(i, DEBUGGING);
		if (!compare(rq, "strict"))
			return pickle_set_result_integer(i, STRICT_NUMERIC_CONVERSION);
		if (!compare(rq, "string-length"))
			return pickle_set_result_integer(i, USE_MAX_STRING ? PICKLE_MAX_STRING : -1);
	}
	return pickle_set_result_error(i, "Invalid subcommand %s", rq);
}

static int picolRegisterCoreCommands(pickle_t *i) {
	assert(i);
	/* NOTE: to save on memory we could do command lookup against this
	 * static table for built in commands, instead of registering them
	 * in the normal way */
	static const pickle_register_command_t commands[] = {
		{ "break",     picolCommandRetCodes,  (char*)PICKLE_BREAK },
		{ "catch",     picolCommandCatch,     NULL },
		{ "concat",    picolCommandConcat,    NULL },
		{ "continue",  picolCommandRetCodes,  (char*)PICKLE_CONTINUE },
		{ "eval",      picolCommandEval,      NULL },
		{ "subst",     picolCommandSubst,     NULL },
		{ "if",        picolCommandIf,        NULL },
		{ "info",      picolCommandInfo,      NULL },
		{ "join",      picolCommandJoin,      NULL },
		{ "join-args", picolCommandJoinArgs,  NULL },
		{ "proc",      picolCommandProc,      NULL },
		{ "return",    picolCommandReturn,    NULL },
		{ "set",       picolCommandSet,       NULL },
		{ "unset",     picolCommandUnSet,     NULL },
		{ "uplevel",   picolCommandUpLevel,   NULL },
		{ "upvar",     picolCommandUpVar,     NULL },
		{ "while",     picolCommandWhile,     NULL },
		{ "for",       picolCommandFor,       NULL },
		{ "rename",    picolCommandRename,    NULL },
		{ "lindex",    picolCommandLIndex,    NULL }, /* TODO: Make list commands optional */
		{ "llength",   picolCommandLLength,   NULL },
	};
	if (DEFINE_STRING)
		if (picolRegisterCommand(i, "string", picolCommandString, NULL) != PICKLE_OK)
			return PICKLE_ERROR;
	if (DEFINE_MATHS) {
		static const char *unary[]  = { [UNOT] = "!", [UINV] = "~", [UABS] = "abs", [UBOOL] = "bool" };
		static const char *binary[] = {
			[BADD]   =  "+",   [BSUB]     =  "-",    [BMUL]     =  "*",    [BDIV]  =  "/",    [BMOD]  =  "%",
			[BMORE]  =  ">",   [BMEQ]     =  ">=",   [BLESS]    =  "<",    [BLEQ]  =  "<=",   [BEQ]   =  "==",
			[BNEQ]   =  "!=",  [BLSHIFT]  =  "<<",   [BRSHIFT]  =  ">>",   [BAND]  =  "&",    [BOR]   =  "|",
			[BXOR]   =  "^",   [BMIN]     =  "min",  [BMAX]     =  "max",  [BPOW]  =  "pow",  [BLOG]  =  "log"
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
	return pickle_set_var_integer(i, "version", VERSION);
}

static void picolFreeCmd(pickle_t *i, struct pickle_command *p) {
	assert(i);
	if (!p)
		return;
	if (p->func == picolCommandCallProc) {
		char **procdata = (char**) p->privdata;
		if (procdata) {
			picolFree(i, procdata[0]);
			picolFree(i, procdata[1]);
		}
		picolFree(i, procdata);
	}
	picolFree(i, p->name);
	picolFree(i, p);
}

static int picolDeinitialize(pickle_t *i) {
	assert(i);
	picolDropAllCallFrames(i);
	assert(!(i->callframe));
	picolFreeResult(i);
	for (long j = 0; j < i->length; j++) {
		struct pickle_command *c = i->table[j], *p = NULL;
		for (; c; p = c, c = c->next) {
			picolFreeCmd(i, p);
			assert(c != c->next);
		}
		picolFreeCmd(i, p);
	}
	picolFree(i, i->table);
	memset(i, 0, sizeof *i);
	return PICKLE_OK;
}

static int picolInitialize(pickle_t *i, const pickle_allocator_t *a) {
	static_assertions();
	assert(i);
	assert(a);
	/*'i' may contain junk, otherwise: assert(!(i->initialized));*/
	const size_t hbytes = PICKLE_MAX_STRING;
	const size_t helem  = hbytes / sizeof (*i->table);
	memset(i, 0, sizeof *i);
	i->initialized   = 1;
	i->allocator     = *a;
	i->callframe     = i->allocator.malloc(i->allocator.arena, sizeof(*i->callframe));
	i->result        = string_empty;
	i->static_result = 1;
	i->table         = picolMalloc(i, hbytes); /* NB. We could make this configurable, for little gain. */

	if (!(i->callframe) || !(i->result) || !(i->table))
		goto fail;
	memset(i->table,     0, hbytes);
	memset(i->callframe, 0, sizeof(*i->callframe));
	i->length = helem;
	if (picolRegisterCoreCommands(i) != PICKLE_OK)
		goto fail;
	return PICKLE_OK;
fail:
	picolDeinitialize(i);
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

int pickle_new(pickle_t **i, const pickle_allocator_t *a) {
	assert(i);
	*i = NULL;
	const pickle_allocator_t *m = a;
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
	return picolInitialize(*i, m);
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
		{  "\\z",           "N/A",    -3  },
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
		memset(m, 0, sizeof m); /* lazy */
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
	if (!(f = concatenate(i, join, argc, argv)) || compare(f, result))
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

static inline int picolTestConvertLong(void) {
	int r = 0;
	long val = 0;
	pickle_t *p = NULL;
	
	static const struct test_t {
		long val;
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
		const int error = picolConvertLong(p, t->string, &val);
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

static inline int picolTestParser(void) { /**@todo The parser needs unit test writing for it */
	int r = 0;
	pickle_parser_t p = { NULL };

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
		} while(p.type != PT_EOF);
	}
	return r;
}

int pickle_tests(void) {
	if (!DEFINE_TESTS) {
		return PICKLE_OK;
	}
	typedef int (*test_func)(void);
	static const test_func ts[] = {
		picolTestSmallString,
		picolTestUnescape,
		picolTestConvertLong,
		picolTestConcat,
		picolTestEval,
		picolTestGetSetVar,
		picolTestLineNumber,
		picolTestParser,
	};
	int r = 0;
	for (size_t i = 0; i < sizeof(ts)/sizeof(ts[0]); i++)
		if (ts[i]() != 0)
			r = -1;
	return r != 0 ? PICKLE_ERROR : PICKLE_OK;
}

