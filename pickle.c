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
 *   - 'picol'  and camelCase  is used for internal functions/variables/types
 *   - Use asserts wherever you can for as many preconditions, postconditions
 *   and invariants that you can think of. 
 *
 * TODO: There are some arbitrary limits on string length, these limits should
 * be removed. The limits mostly come from using a temporary buffer stack
 * allocated with a fixed width. Instead of removing this completely, the
 * buffer should be moved to a heap when it is too big for this buffer.
 * NOTE: An 'expr' command, which would process infix mathematics expressions,
 * could be added but not much would be gained. */

#include "pickle.h"
#include <assert.h>  /* !defined(NDEBUG): assert */
#include <ctype.h>   /* toupper, tolower, isalnum, isalpha, ... */
#include <limits.h>  /* CHAR_BIT */
#include <stdarg.h>  /* va_list, va_start, va_end */
#include <stdio.h>   /* vsnprintf, snprintf. !defined(NDEBUG): puts, printf */
#include <stdlib.h>  /* strtol. !defined(DEFAULT_ALLOCATOR): free, malloc, realloc */
#include <string.h>  /* memcpy, memset, memchr, strstr, strncmp, strncat, strlen */

#ifdef NDEBUG
#define DEBUGGING (0)
#else
#define DEBUGGING (1)
#endif

#define VERSION                  (1989)
#define DEFINE_MATHS             (1)
#define DEFINE_STRING            (1)
#define DEFAULT_ALLOCATOR        (1)
#define USE_MAX_STRING           (1)
#define UNUSED(X)                ((void)(X))
#define BUILD_BUG_ON(condition)  ((void)sizeof(char[1 - 2*!!(condition)]))
#define implies(P, Q)            assert(!(P) || (Q)) /* material implication, immaterial if NDEBUG defined */
#define verify(X)                do { if (!(X)) { abort(); } } while (0)

enum { PT_ESC, PT_STR, PT_CMD, PT_VAR, PT_SEP, PT_EOL, PT_EOF };

struct picolParser {
	const char *text;  /**< the program */
	const char *p;     /**< current text position */
	int len;           /**< remaining length */
	const char *start; /**< token start */
	const char *end;   /**< token end */
	int type;          /**< token type, PT_... */
	int insidequote;   /**< true if inside " " */
	int *line;         /**< pointer to line number */
	const char **ch;   /**< pointer to global test position */
}; /**< Parsing structure */

typedef struct {
	const char *name;           /**< Name of function/TCL command */
	pickle_command_func_t func; /**< Callback that actually does stuff */
	void *data;                 /**< Optional data for this function */
} pickle_register_command_t;        /**< A single TCL command */

enum { PV_STRING, PV_SMALL_STRING, PV_LINK };

typedef union {
	char *ptr,  /**< pointer to string that has spilled over 'small' in size */
	     small[sizeof(char*)]; /**< string small enough to be stored in a pointer (including NUL terminator)*/
} compact_string_t; /**< either a pointer to a string, or a string stored in a pointer */

struct pickle_var { /* strings are stored as either pointers, or as 'small' strings */
	compact_string_t name; /**< name of variable */
	union {
		compact_string_t val;    /**< value */
		struct pickle_var *link; /**< link to another variable */
	} data;
	struct pickle_var *next; /**< next variable in list of variables */
	/* NOTE:
	 * - On a 32 machine type, two bits could merged into the lowest bits
	 *   on the 'next' pointer, as these pointers are most likely aligned
	 *   on a 4 byte boundary, leaving the lowest bits free. However, this
	 *   would be non-portable. There is nothing to be gained from this,
	 *   as we have one bit left over.
	 * - On a 64 bit machine, all three bits could be merged with a
	 *   pointer, saving space in this structure. */
	unsigned type      : 2; /* type of data; string (pointer/small), or link (NB. Could add number type) */
	unsigned smallname : 1; /* if true, name is stored as small string */
};

struct pickle_command {
	/* If online help in the form of help strings were to be added, we
	 * could add another field for it here */
	char *name;                  /**< name of function */
	pickle_command_func_t func;  /**< pointer to function that implements this command */
	struct pickle_command *next; /**< next command in list (chained hash table) */
	void *privdata;              /**< (optional) private data for function */
};

struct pickle_call_frame {       /**< A call frame, organized as a linked list */
	struct pickle_var *vars;          /**< first variable in linked list of variables */
	struct pickle_call_frame *parent; /**< parent is NULL at top level */
};

struct pickle_interpreter { /**< The Pickle Interpreter! */
	pickle_allocator_t allocator;        /**< custom allocator, if desired */
	const char *result;                  /**< result of an evaluation */
	const char *ch;                      /**< the current text position; set if line != 0 */
	struct pickle_call_frame *callframe; /**< internal use only; call stack */
	struct pickle_command **table;       /**< internal use only; hash table */
	long length;                         /**< internal use only; buckets in hash table */
	int level;                           /**< internal use only; level of nesting */
	int line;                            /**< current line number */
	unsigned initialized   :1;           /**< if true, interpreter is initialized and ready to use */
	unsigned static_result :1;           /**< internal use only: if true, result should not be freed */
};

static char 
	*string_empty = "",              /* Space saving measure */
	*string_oom   = "Out Of Memory"; /* Cannot allocate this, obviously */

static inline void static_assertions(void) { /* A neat place to put these */
	BUILD_BUG_ON(PICKLE_MAX_STRING    < 128);
	BUILD_BUG_ON(PICKLE_MAX_RECURSION < 8);
	BUILD_BUG_ON(PICKLE_MAX_ARGS      < 8);
	BUILD_BUG_ON(PICKLE_OK != 0);
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

static inline void picolFree(pickle_t *i, void *p) {
	assert(i);
	assert(i->allocator.free);
	i->allocator.free(i->allocator.arena, p);
}

static inline int compare(const char *a, const char *b) {
	assert(a);
	assert(b);
	if (USE_MAX_STRING)
		return strncmp(a, b, PICKLE_MAX_STRING);
	return strcmp(a, b);
}

static int logarithm(long a, const long b, long *c) {
	assert(c);
	long r = -1;
	*c = r;
	if (a <= 0 || b < 2)
		return PICKLE_ERROR;
	do r++; while (a /= b);
	*c = r;
	return PICKLE_OK;
}

static int power(long base, long exp, long *r) {
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
 * keep them on the stack, but move them when they get too big, we can use
 * the picolStackOrHeapAlloc/picolStackOrHeapFree functions to manage this.
 *
 * @param[in]   orig, pointer to original stack allocation
 * @param[inout] new, pointer we use, which may now point to stack or heap
 * @param[in] length, pointer to length of 'new'
 * @param needed,     required length
 * @return PICKLE_OK on success, PICKLE_ERROR on failure. '*new' is freed
 * on failure if it needs to be. */
static int picolStackOrHeapAlloc(pickle_t *i, char *orig, char **new, size_t *length, size_t needed) {
	assert(i);
	assert(orig);
	assert(new);
	assert(length);
	void *current = *new;
	size_t l = *length;
	if (l <= needed)
		return PICKLE_OK;
	if (USE_MAX_STRING)
		return PICKLE_ERROR;
	if (current == orig) {
		if (!(*new = picolMalloc(i, needed)))
			return PICKLE_ERROR;
		*length = needed;
		return PICKLE_OK;
	}
	if (!(current = picolRealloc(i, current, needed))) {
		picolFree(i, *new);
		return PICKLE_ERROR;
	}
	*new = current;
	*length = needed;
	return PICKLE_OK;
}

static int picolStackOrHeapFree(pickle_t *i, char *orig, char **new) {
	assert(i);
	assert(orig);
	assert(new);
	if (orig != *new)
		picolFree(i, orig);
	*new = NULL;
	return PICKLE_OK;
}

/* Adapted from: <https://stackoverflow.com/questions/10404448>
 *
 * TODO:
 *  - remove need for 'init' field in opt argument
 *  - perhaps the pickle_t object could be used instead of a custom
 *  object.
 *  - more assertions
 *  - add as function to interpreter */
int pickle_getopt(pickle_getopt_t *opt, const int argc, char *const argv[], const char *fmt) {
	assert(opt);
	assert(fmt);
	assert(argv);
	/* enum { BADARG_E = ':', BADCH_E = '?' }; */
	enum { BADARG_E = PICKLE_ERROR, BADCH_E = PICKLE_ERROR };

	if (!(opt->init)) {
		opt->place = string_empty; /* option letter processing */
		opt->init  = 1;
		opt->index = 1;
	}

	if (opt->reset || !*opt->place) { /* update scanning pointer */
		opt->reset = 0;
		if (opt->index >= argc || *(opt->place = argv[opt->index]) != '-') {
			opt->place = string_empty;
			return PICKLE_RETURN;
		}
		if (opt->place[1] && *++opt->place == '-') { /* found "--" */
			opt->index++;
			opt->place = string_empty;
			return PICKLE_RETURN;
		}
	}

	const char *oli = NULL; /* option letter list index */
	if ((opt->option = *opt->place++) == ':' || !(oli = strchr(fmt, opt->option))) { /* option letter okay? */
		 /* if the user didn't specify '-' as an option, assume it means -1.  */
		if (opt->option == '-')
			return PICKLE_RETURN;
		if (!*opt->place)
			opt->index++;
		/*if (opt->error && *fmt != ':')
			(void)fprintf(stderr, "illegal option -- %c\n", opt->option);*/
		return BADCH_E;
	}

	if (*++oli != ':') { /* don't need argument */
		opt->arg = NULL;
		if (!*opt->place)
			opt->index++;
	} else {  /* need an argument */
		if (*opt->place) { /* no white space */
			opt->arg = opt->place;
		} else if (argc <= ++opt->index) { /* no arg */
			opt->place = string_empty;
			if (*fmt == ':')
				return BADARG_E;
			/*if (opt->error)
				(void)fprintf(stderr, "option requires an argument -- %c\n", opt->option);*/
			return BADCH_E;
		} else	{ /* white space */
			opt->arg = argv[opt->index];
		}
		opt->place = string_empty;
		opt->index++;
	}
	return opt->option; /* dump back option letter */
}

static char *picolStrdup(pickle_t *i, const char *s) {
	assert(i);
	assert(s);
	// const size_t l = USE_MAX_STRING ? strnlen(s, PICKLE_MAX_STRING) : strlen(s);
	const size_t l = strlen(s);
	char *r = picolMalloc(i, l + 1);
	return r ? memcpy(r, s, l + 1) : r;
}

static inline unsigned long hash(const char *s, size_t len) { /* DJB2 Hash, <http://www.cse.yorku.ca/~oz/hash.html> */
	assert(s);
	unsigned long h = 5381;
	for (size_t i = 0; i < len; s++, i++)
		h = ((h << 5) + h) + (*s);
	return h;
}

static inline struct pickle_command *picolGetCommand(pickle_t *i, const char *s) {
	assert(s);
	assert(i);
	struct pickle_command *np;
	for (np = i->table[hash(s, strlen(s)) % i->length]; np != NULL; np = np->next)
		if (!compare(s, np->name))
			return np; /* found */
	return NULL; /* not found */
}

static int picolErrorOutOfMemory(pickle_t *i);

/* <https://stackoverflow.com/questions/4384359/> */
int pickle_register_command(pickle_t *i, const char *name, pickle_command_func_t func, void *privdata) {
	assert(i);
	assert(name);
	assert(func);
	struct pickle_command *np;
	if ((np = picolGetCommand(i, name)) == NULL) { /* not found */
		np = picolMalloc(i, sizeof(*np));
		if (np == NULL || (np->name = picolStrdup(i, name)) == NULL) {
			picolFree(i, np);
			return picolErrorOutOfMemory(i);
		}
		/*TODO: Allow removal of values */
		const unsigned long hashval = hash(name, strlen(name)) % i->length;
		np->next = i->table[hashval];
		i->table[hashval] = np;
	} else { /* already there */
		return pickle_set_result_error(i, "'%s' already defined", name);
	}
	np->func = func;
	np->privdata = privdata;
	return PICKLE_OK;
}

static int advance(struct picolParser *p) {
	assert(p);
	if (p->len <= 0)
		return PICKLE_ERROR;
	if (p->len && !(*p->p))
		return PICKLE_ERROR;
	if (p->line && *p->line/*0 disables line count*/ && *p->ch < p->p) {
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

static inline void picolParserInitialize(struct picolParser *p, const char *text, int *line, const char **ch) {
	assert(p);
	assert(text);
	memset(p, 0, sizeof *p);
	p->text = text;
	p->p    = text;
	p->len  = strlen(text);
	p->type = PT_EOL;
	p->line = line;
	p->ch   = ch;
}

static inline int picolIsSpaceChar(int ch) {
	return (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r');
}

static inline int picolParseSep(struct picolParser *p) {
	assert(p);
	p->start = p->p;
	while (picolIsSpaceChar(*p->p))
		if (advance(p) != PICKLE_OK)
			return PICKLE_ERROR;
	p->end  = p->p - 1;
	p->type = PT_SEP;
	return PICKLE_OK;
}

static inline int picolParseEol(struct picolParser *p) {
	assert(p);
	p->start = p->p;
	while (picolIsSpaceChar(*p->p) || *p->p == ';')
		if (advance(p) != PICKLE_OK)
			return PICKLE_ERROR;
	p->end  = p->p - 1;
	p->type = PT_EOL;
	return PICKLE_OK;
}

static inline int picolParseCommand(struct picolParser *p) {
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

static inline int picolParseVar(struct picolParser *p) {
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

static inline int picolParseBrace(struct picolParser *p) {
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

static int picolParseString(struct picolParser *p) {
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
			if (p->len >= 2)
				if (advance(p) != PICKLE_OK)
					return PICKLE_ERROR;
			break;
		case '$': case '[':
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

static inline int picolParseComment(struct picolParser *p) {
	assert(p);
	while (p->len && *p->p != '\n')
		if (advance(p) != PICKLE_OK)
			return PICKLE_ERROR;
	return PICKLE_OK;
}

static int picolGetToken(struct picolParser *p) {
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
		case '[':
			return picolParseCommand(p);
		case '$':
			return picolParseVar(p);
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

static void picolFreeResult(pickle_t *i) {
	assert(i);
	if (!(i->static_result))
		picolFree(i, (char*)i->result);
}

static int picolErrorOutOfMemory(pickle_t *i) { /* does not allocate */
	assert(i);
	picolFreeResult(i);
	i->result = string_oom;
	i->static_result = 1;
	return PICKLE_ERROR;
}

static int picolSetResultEmpty(pickle_t *i) {
	assert(i);
	picolFreeResult(i);
	i->result = string_empty;
	i->static_result = 1;
	return PICKLE_OK;
}

int pickle_set_result_string(pickle_t *i, const char *s) {
	assert(i);
	assert(i->result);
	assert(s);
	char *r = picolStrdup(i, s);
	if (r) {
		picolFreeResult(i);
		i->static_result = 0;
		i->result = r;
		return PICKLE_OK;
	}
	return picolErrorOutOfMemory(i);
}

int pickle_get_result_string(pickle_t *i, const char **s) {
	assert(i);
	assert(s);
	assert(i->result);
	*s = i->result;
	return PICKLE_OK;
}

int pickle_get_result_integer(pickle_t *i, long *val) {
	assert(val);
	*val = atol(i->result);
	return PICKLE_OK;
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

static inline int picolIsSmallString(const char *val) {
	assert(val);
	return !!memchr(val, 0, sizeof(char*));
}

static int picolSetVarString(pickle_t *i, struct pickle_var *v, const char *val) {
	assert(i);
	assert(v);
	assert(val);
	if (picolIsSmallString(val)) {
		v->type = PV_SMALL_STRING;
		memset(v->data.val.small, 0,    sizeof(v->data.val.small));
		strncat(v->data.val.small, val, sizeof(v->data.val.small) - 1);
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
		memset(v->name.small, 0,     sizeof(v->name.small));
		strncat(v->name.small, name, sizeof(v->name.small) - 1);
		return PICKLE_OK;
	}
	v->smallname = 0;
	return (v->name.ptr = picolStrdup(i, name)) ? PICKLE_OK : PICKLE_ERROR;
}

int pickle_set_var_string(pickle_t *i, const char *name, const char *val) {
	assert(i);
	assert(name);
	assert(val);
	struct pickle_var *v = picolGetVar(i, name, 1);
	if (v) {
		picolFreeVarVal(i, v);
		if (picolSetVarString(i, v, val) != PICKLE_OK)
			return picolErrorOutOfMemory(i);
	} else {
		if (!(v = picolMalloc(i, sizeof(*v))))
			return picolErrorOutOfMemory(i);
		const int r1 = picolSetVarName(i, v, name);
		const int r2 = picolSetVarString(i, v, val);
		if (r1 != PICKLE_OK || r2 != PICKLE_OK) {
			picolFreeVarName(i, v);
			picolFreeVarVal(i, v);
			picolFree(i, v);
			return picolErrorOutOfMemory(i);
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
	assert(val);
	*val = 0;
	const char *s = NULL;
	const int retcode = pickle_get_var_string(i, name, &s);
	if (!s || retcode != PICKLE_OK)
		return PICKLE_ERROR;
	*val = atol(s);
	return PICKLE_OK;
}

int pickle_set_result_error(pickle_t *i, const char *fmt, ...) {
	assert(i);
	assert(fmt);
	size_t off = 0;
	char errbuf[PICKLE_MAX_STRING] = { 0 };
	if (i->line)
		off = snprintf(errbuf, sizeof(errbuf) / 2, "line %d: ", i->line);
	assert(off < PICKLE_MAX_STRING);
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(errbuf + off, sizeof(errbuf) - off, fmt, ap);
	va_end(ap);
	(void)pickle_set_result_string(i, errbuf);
	return PICKLE_ERROR;
}

int pickle_set_result(pickle_t *i, const char *fmt, ...) {
	assert(i);
	assert(fmt);
	char buf[PICKLE_MAX_STRING] = { 0 };
	va_list ap;
	va_start(ap, fmt);
	const int r = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	return r < 0 ? 
		pickle_set_result_error(i, "vsnprintf: invalid format") : 
		pickle_set_result_string(i, buf);
}

int pickle_set_result_integer(pickle_t *i, const long result) {
	assert(i);
	char buffy[64] = { 0 };
	const int r = snprintf(buffy, sizeof(buffy), "%ld", result); (void)r;
	assert(r >= 0 && r <= (int)(sizeof buffy));
	return pickle_set_result_string(i, buffy);
}

int pickle_set_var_integer(pickle_t *i, const char *name, const long r) {
	char v[64] = { 0 };
	snprintf(v, sizeof v, "%ld", r);
	return pickle_set_var_string(i, name, v);
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

static void picolFreeArgList(pickle_t *i, const int argc, char **argv) {
	assert(i);
	assert(argc >= 0);
	implies(argc != 0, argv);
	for (int j = 0; j < argc; j++)
		picolFree(i, argv[j]);
	picolFree(i, argv);
}

static int picolUnEscape(char *inout) {
	assert(inout);
	int k = 0;
	char r[PICKLE_MAX_STRING] = { 0 };
	for (int j = 0, ch = 0; (ch = inout[j]); j++, k++) {
		if (ch == '\\') {
			j++;
			switch (inout[j]) {
			case '\\': r[k] = '\\'; break;
			case  'n': r[k] = '\n'; break;
			case  't': r[k] = '\t'; break;
			case  'r': r[k] = '\r'; break;
			case  '"': r[k] = '"';  break;
			case  '[': r[k] = '[';  break;
			case  ']': r[k] = ']';  break;
			case  'e': r[k] = 27;   break;
			case  'x': {
				if (!inout[j + 1] || !inout[j + 2])
					return -1;
				const char v[3] = { inout[j + 1], inout[j + 2], 0 };
				unsigned d = 0;
				int pos = 0;
				if (sscanf(v, "%x%n", &d, &pos) != 1)
					return -2;
				assert(pos > 0 && pos < 3);
				j += pos;
				r[k] = d;
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
	memcpy(inout, r, k + 1);
	return k;
}

static int picolEval(pickle_t *i, const char *t) {
	assert(i);
	assert(i->initialized);
	assert(t);
	struct picolParser p = { NULL };
	int retcode = PICKLE_OK, argc = 0;
	char **argv = NULL;
	if (picolSetResultEmpty(i) != PICKLE_OK)
		return PICKLE_ERROR;
	picolParserInitialize(&p, t, &i->line, &i->ch);
	int prevtype = p.type;
	for (;;) { /**@todo separate out the code so it can be reused in a 'subst' command */
		if (picolGetToken(&p) != PICKLE_OK)
			return pickle_set_result_error(i, "parser error");
		if (p.type == PT_EOF)
			break;
		int tlen = p.end - p.start + 1;
		if (tlen < 0)
			tlen = 0;
		char *t = picolMalloc(i, tlen + 1);
		if (!t) {
			retcode = picolErrorOutOfMemory(i);
			goto err;
		}
		memcpy(t, p.start, tlen);
		t[tlen] = '\0';
		if (p.type == PT_VAR) {
			struct pickle_var * const v = picolGetVar(i, t, 1);
			if (!v) {
				retcode = pickle_set_result_error(i, "No such variable '%s'", t);
				picolFree(i, t);
				goto err;
			}
			picolFree(i, t);
			if (!(t = picolStrdup(i, picolGetVarVal(v)))) {
				retcode = picolErrorOutOfMemory(i);
				goto err;
			}
		} else if (p.type == PT_CMD) {
			retcode = picolEval(i, t);
			picolFree(i, t);
			if (retcode != PICKLE_OK)
				goto err;
			if (!(t = picolStrdup(i, i->result))) {
				retcode = picolErrorOutOfMemory(i);
				goto err;
			}
		} else if (p.type == PT_ESC) {
			if (picolUnEscape(t) < 0) {
				retcode = pickle_set_result_error(i, "Invalid escape sequence '%s'", t);
				picolFree(i, t);
				goto err;
			}
		} else if (p.type == PT_SEP) {
			prevtype = p.type;
			picolFree(i, t);
			continue;
		}

		if (p.type == PT_EOL) { /* We have a complete command + args. Call it! */
			struct pickle_command *c = NULL;
			picolFree(i, t);
			prevtype = p.type;
			if (argc) {
				if ((c = picolGetCommand(i, argv[0])) == NULL) {
					retcode = pickle_set_result_error(i, "No such command '%s'", argv[0]);
					goto err;
				}
				picolAssertCommandPreConditions(i, argc, argv, c->privdata);
				retcode = c->func(i, argc, argv, c->privdata);
				picolAssertCommandPostConditions(i, retcode);
				if (retcode != PICKLE_OK)
					goto err;
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
				retcode = picolErrorOutOfMemory(i);
				goto err;
			}
			argv[argc] = t;
			t = NULL;
			argc++;
		} else { /* Interpolation */
			const int oldlen = strlen(argv[argc - 1]), tlen = strlen(t);
			char *arg = picolRealloc(i, argv[argc - 1], oldlen + tlen + 1);
			if (!arg) {
				retcode = picolErrorOutOfMemory(i);
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

int pickle_eval(pickle_t *i, const char *t) {
	assert(i);
	assert(t);
	i->line = 1;
	i->ch   = t;
	return picolEval(i, t);
}

static char *concatenate(pickle_t *i, const char *join, const int argc, char **argv) {
	assert(i);
	assert(join);
	assert(argc >= 0);
	implies(argc > 0, argv != NULL);
	if (argc == 0)
		return picolStrdup(i, "");
	if (argc > (int)PICKLE_MAX_ARGS)
		return NULL;
	const size_t jl = strlen(join);
	size_t ls[PICKLE_MAX_ARGS], l = 0;
	for (int j = 0; j < argc; j++) {
		const size_t sz = strlen(argv[j]);
		ls[j] = sz;
		l += sz + jl;
	}
	if (USE_MAX_STRING && ((l + 1) >= PICKLE_MAX_STRING))
		return NULL;
	char buf[PICKLE_MAX_STRING] = { 0 };
	char *r = &buf[0];
	size_t length = sizeof buf;
	if (l > (PICKLE_MAX_STRING - 1))
		if (picolStackOrHeapAlloc(i, buf, &r, &length, l) < 0)
			return NULL;
	l = 0;
	for (int j = 0; j < argc; j++) {
		assert(!USE_MAX_STRING || l < PICKLE_MAX_STRING);
		memcpy(r + l, argv[j], ls[j]);
		l += ls[j];
		if (jl && (j + 1) < argc) {
			assert(!USE_MAX_STRING || l < PICKLE_MAX_STRING);
			memcpy(r + l, join, jl);
			l += jl;
		}
	}
	r[l] = '\0';
	if (r != buf)
		return r;
	char *str = picolStrdup(i, r);
	picolStackOrHeapFree(i, buf, &r);
	return str;
}

int pickle_set_result_error_arity(pickle_t *i, const int expected, const int argc, char **argv) {
	assert(i);
	assert(argc >= 1);
	assert(argv);
	char *as = concatenate(i, " ", argc, argv);
	if (!as)
		return picolErrorOutOfMemory(i);
	const int r = pickle_set_result_error(i, "Wrong number of args for '%s' (expected %d)\nGot: %s", argv[0], expected - 1, as);
	picolFree(i, as);
	return r;
}

/*Based on: <http://c-faq.com/lib/regex.html>, also see:
 <https://www.cs.princeton.edu/courses/archive/spr09/cos333/beautiful.html> */
static int match(const char *pat, const char *str, size_t depth) {
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

static const char *trimleft(const char *class, const char *s) { /* Returns pointer to s */
	assert(class);
	assert(s);
	size_t j = 0, k = 0;
	while (s[j] && strchr(class, s[j++]))
		k = j;
	return &s[k];
}

static void trimright(const char *class, char *s) { /* Modifies argument */
	assert(class);
	assert(s);
	const size_t length = strlen(s);
	size_t j = length - 1;
	if (j > length)
		return;
	while (j > 0 && strchr(class, s[j]))
		j--;
	if (s[j])
		s[j + !strchr(class, s[j])] = 0;
}

static inline void swap(char * const a, char * const b) {
	assert(a);
	assert(b);
	const char t = *a;
	*a = *b;
	*b = t;
}

static char *reverse(char *s, size_t length) { /* Modifies Argument */
	assert(s);
	for (size_t i = 0; i < (length/2); i++)
		swap(&s[i], &s[(length - i) - 1]);
	return s;
}

static int picolCommandString(pickle_t *i, const int argc, char **argv, void *pd) { /* Big! */
	UNUSED(pd);
	if (argc < 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	const char *rq = argv[1];
	char buf[PICKLE_MAX_STRING] = { 0 };
	if (argc == 3) {
		const char *arg1 = argv[2];
		static const char *space = " \t\n\r\v";
		if (!compare(rq, "trimleft"))
			return pickle_set_result_string(i, trimleft(space, arg1));
		if (!compare(rq, "trimright")) {
			strncpy(buf, arg1, PICKLE_MAX_STRING);
			trimright(space, buf);
			return pickle_set_result_string(i, buf);
		}
		if (!compare(rq, "trim"))      {
			strncpy(buf, arg1, PICKLE_MAX_STRING);
			trimright(space, buf);
			return pickle_set_result_string(i, trimleft(space, buf));
		}
		if (!compare(rq, "length"))
			return pickle_set_result_integer(i, strlen(arg1));
		if (!compare(rq, "toupper")) {
			size_t j = 0;
			for (j = 0; arg1[j]; j++)
				buf[j] = toupper(arg1[j]);
			buf[j] = 0;
			return pickle_set_result_string(i, buf);
		}
		if (!compare(rq, "tolower")) {
			size_t j = 0;
			for (j = 0; arg1[j]; j++)
				buf[j] = tolower(arg1[j]);
			buf[j] = 0;
			return pickle_set_result_string(i, buf);
		}
		if (!compare(rq, "reverse")) {
			const size_t l = strlen(arg1);
			memcpy(buf, arg1, l + 1);
			return pickle_set_result_string(i, reverse(buf, l));
		}
		if (!compare(rq, "ordinal"))
			return pickle_set_result_integer(i, arg1[0]);
		if (!compare(rq, "char")) {
			buf[0] = atoi(arg1);
			buf[1] = 0;
			return pickle_set_result_string(i, buf);
		}
		if (!compare(rq, "dec2hex")) {
			if (snprintf(buf, sizeof buf, "%lx", atol(arg1)) < 1)
				return pickle_set_result_error(i, "snprintf format error '%%lx'");
			return pickle_set_result_string(i, buf);
		}
		if (!compare(rq, "hex2dec")) {
			char *ep = NULL;
			const long l = strtol(arg1, &ep, 16);
			if (*arg1 && !*ep)
				return pickle_set_result_integer(i, l);
			return pickle_set_result_error(i, "Invalid hexadecimal value: %s", arg1);
		}
		if (!compare(rq, "hash"))
			return pickle_set_result_integer(i, hash(arg1, strlen(arg1)));
	} else if (argc == 4) {
		const char *arg1 = argv[2], *arg2 = argv[3];
		if (!compare(rq, "trimleft"))
			return pickle_set_result_string(i, trimleft(arg2, arg1));
		if (!compare(rq, "trimright")) {
			strncpy(buf, arg1, PICKLE_MAX_STRING);
			trimright(arg2, buf);
			return pickle_set_result_string(i, buf);
		}
		if (!compare(rq, "trim"))   {
			strncpy(buf, arg1, PICKLE_MAX_STRING);
			trimright(arg2, buf);
			return pickle_set_result_string(i, trimleft(arg2, buf));
		}
		if (!compare(rq, "match"))  {
			const int r = match(arg1, arg2, PICKLE_MAX_RECURSION - i->level);
			if (r < 0)
				return pickle_set_result_error(i, "Regex error: %d", r);
			return pickle_set_result_integer(i, r);
		}
		if (!compare(rq, "equal"))
			return pickle_set_result_integer(i, !compare(arg1, arg2));
		if (!compare(rq, "compare"))
			return pickle_set_result_integer(i, compare(arg1, arg2));
		if (!compare(rq, "index"))   {
			long index = atol(arg2);
			const long length = strlen(arg1);
			if (index < 0)
				index = length + index;
			if (index > length)
				index = length - 1;
			if (index < 0)
				index = 0;
			const char ch[2] = { arg1[index], 0 };
			return pickle_set_result_string(i, ch);
		}
		if (!compare(rq, "is")) {
			if (!compare(arg1, "alnum"))  { while (isalnum(*arg2))  arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "alpha"))  { while (isalpha(*arg2))  arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "digit"))  { while (isdigit(*arg2))  arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "graph"))  { while (isgraph(*arg2))  arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "lower"))  { while (islower(*arg2))  arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "print"))  { while (isprint(*arg2))  arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "punct"))  { while (ispunct(*arg2))  arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "space"))  { while (isspace(*arg2))  arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "upper"))  { while (isupper(*arg2))  arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "xdigit")) { while (isxdigit(*arg2)) arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "ascii"))  { while (*arg2 && !(0x80 & *arg2)) arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "control")) { while (*arg2 && iscntrl(*arg2)) arg2++; return pickle_set_result_integer(i, !*arg2); }
			if (!compare(arg1, "integer")) {
				char *ep = NULL;
				(void)strtol(arg2, &ep, 10);
				return pickle_set_result_integer(i, *arg2 && !isspace(*arg2) && !*ep);
			}
			/* Missing: double, Boolean, true, false */
		}
		if (!compare(rq, "repeat")) {
			long count = atol(arg2), j = 0;
			const size_t length = strlen(arg1);
			if (count < 0)
				return pickle_set_result_error(i, "'string' repeat count negative: %ld", count);
			if ((count * length) > (PICKLE_MAX_STRING - 1))
				return picolErrorOutOfMemory(i);
			for (; j < count; j++) {
				assert(((j * length) + length) < PICKLE_MAX_STRING);
				memcpy(&buf[j * length], arg1, length);
			}
			buf[j * length] = 0;
			return pickle_set_result_string(i, buf);
		}
		if (!compare(rq, "first"))      {
			const char *found = strstr(arg2, arg1);
			if (!found)
				return pickle_set_result_integer(i, -1);
			return pickle_set_result_integer(i, found - arg2);
		}
	} else if (argc == 5) {
		const char *arg1 = argv[2], *arg2 = argv[3], *arg3 = argv[4];
		if (!compare(rq, "first"))      {
			const long length = strlen(arg2);
			const long start  = atol(arg3);
			if (start < 0 || start >= length)
				return picolSetResultEmpty(i);
			const char *found = strstr(arg2 + start, arg1);
			if (!found)
				return pickle_set_result_integer(i, -1);
			return pickle_set_result_integer(i, found - arg2);
		}
		if (!compare(rq, "range")) {
			const long length = strlen(arg1);
			long first = atol(arg2);
			long last  = atol(arg3);
			if (first > last)
				return picolSetResultEmpty(i);
			if (first < 0)
				first = 0;
			if (last > length)
				last = length;
			const long diff = (last - first) + 1;
			assert(diff < PICKLE_MAX_STRING);
			memcpy(buf, &arg1[first], diff);
			buf[diff] = 0;
			return pickle_set_result_string(i, buf);
		}
	}
	return pickle_set_result_error_arity(i, 3, argc, argv);
}

static int picolCommandMathUnary(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	char *op = argv[0];
	long a = atol(argv[1]);
	if (op[0] == '!')              { a = !a; }
	else if (op[0] == '~')         { a = ~a; }
	else if (!compare(op, "abs"))  { a = a < 0 ? -a : a; }
	else if (!compare(op, "bool")) { a = !!a; }
	else return pickle_set_result_error(i, "Unknown operator %s", op);
	return pickle_set_result_integer(i, a);
}

static int picolCommandMath(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	const char *op = argv[0];
	const long a = atol(argv[1]), b = atol(argv[2]); 
	long c = 0;
	if (op[0] == '+') c = a + b;
	else if (op[0] == '-') c = a - b;
	else if (op[0] == '*') c = a * b;
	else if (op[0] == '/') { if (b) { c = a / b; } else { return pickle_set_result_error(i, "Division by 0"); } }
	else if (op[0] == '%') { if (b) { c = a % b; } else { return pickle_set_result_error(i, "Division by 0"); } }
	else if (op[0] == '>' && op[1] == '\0') c = a >  b;
	else if (op[0] == '>' && op[1] == '=')  c = a >= b;
	else if (op[0] == '<' && op[1] == '\0') c = a <  b;
	else if (op[0] == '<' && op[1] == '=')  c = a <= b;
	else if (op[0] == '=' && op[1] == '=')  c = a == b;
	else if (op[0] == '!' && op[1] == '=')  c = a != b;
	else if (op[0] == '<' && op[1] == '<')  c = (unsigned)a << (unsigned)b;
	else if (op[0] == '>' && op[1] == '>')  c = (unsigned)a >> (unsigned)b;
	else if (op[0] == '&') c = a & b;
	else if (op[0] == '|') c = a | b;
	else if (op[0] == '^') c = a ^ b;
	else if (!compare(op, "min")) c = a < b ? a : b;
	else if (!compare(op, "max")) c = a > b ? a : b;
	else if (!compare(op, "pow")) { if (power(a, b, &c)     != PICKLE_OK) return pickle_set_result_error(i, "Invalid power"); }
	else if (!compare(op, "log")) { if (logarithm(a, b, &c) != PICKLE_OK) return pickle_set_result_error(i, "Invalid logarithm"); }
	else return pickle_set_result_error(i, "Unknown operator %s", op);
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
			return pickle_set_result_error(i, "No such variable: %s", argv[1]);
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
	const int r = picolEval(i, argv[1]);
	return pickle_set_var_integer(i, argv[2], r);
}

static int picolCommandIf(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	int retcode = 0;
	if (argc != 3 && argc != 5)
		return pickle_set_result_error_arity(i, 5, argc, argv);
	if ((retcode = picolEval(i, argv[1])) != PICKLE_OK)
		return retcode;
	if (atol(i->result))
		return picolEval(i, argv[2]);
	else if (argc == 5)
		return picolEval(i, argv[4]);
	return PICKLE_OK;
}

static int picolCommandWhile(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	for (;;) {
		const int r1 = picolEval(i, argv[1]);
		if (r1 != PICKLE_OK)
			return r1;
		if (!atol(i->result))
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
		return pickle_set_result_error(i, "Recursion limit exceed (%d)", PICKLE_MAX_RECURSION);
	char **x = pd, *alist = x[0], *body = x[1], *p = picolStrdup(i, alist), *tofree = NULL;
	int arity = 0, errcode = PICKLE_OK;
	struct pickle_call_frame *cf = picolMalloc(i, sizeof(*cf));
	if (!cf || !p) {
		picolFree(i, p);
		picolFree(i, cf);
		return picolErrorOutOfMemory(i);
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
			return picolErrorOutOfMemory(i);
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
	pickle_set_result_error(i, "Proc '%s' called with wrong arg num", argv[0]);
	picolDropCallFrame(i);
	return PICKLE_ERROR;
}

/* NOTE: If space is really at a premium it would be possible to store the
 * strings compressed, decompressing them when needed. Perhaps 'smaz' library,
 * with a custom dictionary, could be used for this, see
 * <https://github.com/antirez/smaz> for more information. */
static int picolCommandProc(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 4)
		return pickle_set_result_error_arity(i, 4, argc, argv);
	char **procdata = picolMalloc(i, sizeof(char*)*2);
	if (!procdata)
		return picolErrorOutOfMemory(i);
	procdata[0] = picolStrdup(i, argv[2]); /* arguments list */
	procdata[1] = picolStrdup(i, argv[3]); /* procedure body */
	if (!(procdata[0]) || !(procdata[1])) {
		picolFree(i, procdata[0]);
		picolFree(i, procdata[1]);
		picolFree(i, procdata);
		return picolErrorOutOfMemory(i);
	}
	return pickle_register_command(i, argv[1], picolCommandCallProc, procdata);
}

static int picolCommandReturn(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 1 && argc != 2 && argc != 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	int retcode = PICKLE_RETURN;
	if (argc == 3)
		retcode = atol(argv[2]);
	if (argc == 1)
		return picolSetResultEmpty(i) != PICKLE_OK ? PICKLE_ERROR : PICKLE_RETURN;
	if (pickle_set_result_string(i, argv[1]) != PICKLE_OK)
		return PICKLE_ERROR;
	return retcode;
}

static int doJoin(pickle_t *i, const char *join, const int argc, char **argv) {
	char *e = concatenate(i, join, argc, argv);
	if (!e)
		return picolErrorOutOfMemory(i);
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
	int count = 0;
	char *arguments[PICKLE_MAX_ARGS] = { 0 };
	struct picolParser p = { NULL };
	picolParserInitialize(&p, parse, NULL, NULL);
	for (;;) {
		if (picolGetToken(&p) == PICKLE_ERROR)
			return pickle_set_result_error(i, "parser error");
		if (p.type == PT_EOF)
			break;
		if (p.type == PT_EOL)
			continue;
		if (p.type != PT_SEP) {
			char buf[PICKLE_MAX_STRING] = { 0 };
			const size_t l = (p.end - p.start) + 1;
			memcpy(buf, p.start, l);
			buf[l] = 0;
			if (count >= PICKLE_MAX_ARGS)
				return pickle_set_result_error(i, "Argument count exceeded : %d", count);
			arguments[count++] = picolStrdup(i, buf);
		}
	}
	const int r = doJoin(i, join, count, arguments);
	for (int j = 0; j < count; j++)
		picolFree(i, arguments[j]);
	return r;
}

static int picolCommandEval(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	int r = doJoin(i, " ", argc - 1, argv + 1);
	if (r == PICKLE_OK) {
		char *e = picolStrdup(i, i->result);
		if (!e)
			return picolErrorOutOfMemory(i);
		r = picolEval(i, e);
		picolFree(i, e);
	}
	return r;
}

static int picolSetLevel(pickle_t *i, const char *levelStr) {
	const int top = levelStr[0] == '#';
	int level = atol(top ? &levelStr[1] : levelStr);
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
		pickle_set_result_error(i, "Variable '%s' already exists", argv[3]);
		goto end;
	}

	struct pickle_var *myVar = cf->vars, *otherVar = NULL;

	if ((retcode = picolSetLevel(i, argv[1])) != PICKLE_OK)
		goto end;
	if (!(otherVar = picolGetVar(i, argv[2], 1))) {
		if (pickle_set_var_string(i, argv[2], "") != PICKLE_OK)
			return picolErrorOutOfMemory(i);
		otherVar = i->callframe->vars;
	}

	if (myVar == otherVar) { /* more advance cycle detection should be done here */
		pickle_set_result_error(i, "Cannot create circular reference variable '%s'", argv[3]);
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
	int retcode = PICKLE_OK;
	if ((retcode = picolSetLevel(i, argv[1])) == PICKLE_OK) {
		char *e = concatenate(i, " ", argc - 2, argv + 2);
		if (!e) {
			retcode = picolErrorOutOfMemory(i);
			goto end;
		}
		retcode = picolEval(i, e);
		picolFree(i, e);
	}
end:
	i->callframe = cf;
	return retcode;
}

static int picolCommandUnSet(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	struct pickle_call_frame *cf = i->callframe;
	struct pickle_var *p = NULL, *deleteMe = picolGetVar(i, argv[1], 0/*NB!*/);
	if (!deleteMe)
		return pickle_set_result_error(i, "Cannot unset '%s', no such variable", argv[1]);

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
	const long r = atol(argv[2]);
       	long j = 0;
	struct pickle_command *c = NULL;

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
		return pickle_set_result_error(i, "Invalid command index '%ld'", r);
	assert(c);
	const int defined = c->func == picolCommandCallProc;
	const char *rq = argv[1];
	if (!compare(rq, "args")) {
		if (!defined)
			return pickle_set_result_string(i, "built-in");
		char **procdata = c->privdata;
		return pickle_set_result_string(i, procdata[0]);
	} else if (!compare(rq, "body")) {
		if (!defined)
			return pickle_set_result_string(i, "built-in");
		char **procdata = c->privdata;
		return pickle_set_result_string(i, procdata[1]);
	} else if (!compare(rq, "name")) {
		return pickle_set_result_string(i, c->name);
	}
	return pickle_set_result_error(i, "Unknown command request '%s'", rq);
}

static int picolCommandInfo(pickle_t *i, const int argc, char **argv, void *pd) {
	if (argc < 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	const char *rq = argv[1];
	if (!strcmp(rq, "command"))
		return picolCommandCommand(i, argc - 1, argv + 1, pd);
	if (!strcmp(rq, "line"))
		return pickle_set_result_integer(i, i->line);
	if (!strcmp(rq, "level"))
		return pickle_set_result_integer(i, i->level);
	if (!strcmp(rq, "width"))
		return pickle_set_result_integer(i, CHAR_BIT * sizeof(char*));
	if (argc < 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	if (!strcmp(rq, "limits")) {
		rq = argv[2];
		if (!strcmp(rq, "recursion"))
			return pickle_set_result_integer(i, PICKLE_MAX_RECURSION);
		if (!strcmp(rq, "string"))
			return pickle_set_result_integer(i, PICKLE_MAX_STRING);
		if (!strcmp(rq, "arguments"))
			return pickle_set_result_integer(i, PICKLE_MAX_STRING);
	}
	return pickle_set_result_error(i, "Unknown info request '%s'", rq);
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
	};
	if (DEFINE_STRING)
		if (pickle_register_command(i, "string", picolCommandString, NULL) != PICKLE_OK)
			return PICKLE_ERROR;
	if (DEFINE_MATHS) {
		static const char *unary[]  = { "!", "~", "abs", "bool" };
		static const char *binary[] = {
			"+", "-", "*", "/", "%", ">", ">=", "<", "<=", "==", "!=",
			"<<", ">>", "&", "|", "^", "min", "max", "pow", "log"
		};
		for (size_t j = 0; j < sizeof(unary)/sizeof(char*); j++)
			if (pickle_register_command(i, unary[j], picolCommandMathUnary, NULL) != PICKLE_OK)
				return PICKLE_ERROR;
		for (size_t j = 0; j < sizeof(binary)/sizeof(char*); j++)
			if (pickle_register_command(i, binary[j], picolCommandMath, NULL) != PICKLE_OK)
				return PICKLE_ERROR;
	}
	for (size_t j = 0; j < sizeof(commands)/sizeof(commands[0]); j++)
		if (pickle_register_command(i, commands[j].name, commands[j].func, commands[j].data) != PICKLE_OK)
			return PICKLE_ERROR;
	return pickle_set_var_integer(i, "version", VERSION);
}

static void pickleFreeCmd(pickle_t *i, struct pickle_command *p) {
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
			pickleFreeCmd(i, p);
			assert(c != c->next);
		}
		pickleFreeCmd(i, p);
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
	memset(i, 0, sizeof *i);
	i->initialized   = 1;
	i->allocator     = *a;
	i->callframe     = i->allocator.malloc(i->allocator.arena, sizeof(*i->callframe));
	i->result        = string_empty;
	i->static_result = 1;
	i->table         = picolMalloc(i, PICKLE_MAX_STRING);

	if (!(i->callframe) || !(i->result) || !(i->table))
		goto fail;
	memset(i->table,     0, PICKLE_MAX_STRING);
	memset(i->callframe, 0, sizeof(*i->callframe));
	i->length = PICKLE_MAX_STRING/sizeof(*i->table);
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

static inline void pfree(void *arena, void *ptr) {
	UNUSED(arena); assert(arena == NULL);
	free(ptr);
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
	const pickle_allocator_t a = i->allocator;
	const int r = picolDeinitialize(i);
	a.free(a.arena, i);
	return r != PICKLE_OK ? r : PICKLE_OK;
}

#ifdef NDEBUG
int pickle_tests(void) { return PICKLE_OK; }
#else

static inline const char *failed(int n) {
	return n == 0 ? "PASS" : "FAIL";
}

static int test(const char *eval, const char *result, int retcode) {
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
	if (r == 0) {
		printf("        ok %d : %s  = %s\n", retcode, eval, result);
	} else {
		printf("      FAIL %d : %s != %s\n", actual, eval, p->result ? p->result : "(null)");
		printf(" wanted -> %d : %s  = %s\n", retcode, eval, result);
	}
	pickle_delete(p);
	return r;
}

static int picolTestSmallString(void) {
	int r = 0;
	printf("Small String Tests\n");

	if (!picolIsSmallString(""))  { r = -1; }
	if (!picolIsSmallString("0")) { r = -2; }
	if (picolIsSmallString("Once upon a midnight dreary")) { r = -3; }

	printf("Small String Test: %s/%d\n", failed(r), r);
	return r;
}

static int picolTestUnescape(void) {
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
		{  "\\xZ",          "N/A",    -1  },
		{  "\\xZZ",         "N/A",    -2  },
		{  "\\x9Z",         "\011Z",  2   },
		{  "\\x300",        "00",     2   },
		{  "\\x310",        "10",     2   },
		{  "\\x31\\x312",   "112",    3   },
		{  "x\\x31\\x312",  "x112",   4   },
	};

	printf("Unescape Tests\n");
	for (size_t i = 0; i < sizeof(ts)/sizeof(ts[0]); i++) {
		memset(m, 0, sizeof m); /* lazy */
		strncpy(m, ts[i].str, sizeof(m) - 1);
		printf("\tTest: '%s' = '%s' or code %d ...", ts[i].str, ts[i].res, ts[i].r);
		const int u = picolUnEscape(m);
		if (ts[i].r != u) {
			printf("unexpected error code: %d\n", u);
			r = -1;
			continue;
		}
		if (u < 0) {
			printf("ok (code)\n");
			continue;
		}
		if (compare(m, ts[i].res)) {
			printf("not equal: %s\n", m);
			r = -2;
			continue;
		}
		printf("ok\n");
	}
	printf("Unescape Test: %s/%d\n", failed(r), r);
	return r;
}

static int concatenateTest(pickle_t *i, char *result, char *join, int argc, char **argv) {
	int r = PICKLE_OK;
	char *f = NULL;
	if (!(f = concatenate(i, join, argc, argv)) || compare(f, result))
		r = PICKLE_ERROR;
	printf("%s/%s\n", f, result);
	picolFree(i, f);
	return r;
}

static int picolTestConcat(void) {
	int r = 0;
	pickle_t *p = NULL;
	if (pickle_new(&p, NULL) != PICKLE_OK || !p)
		return -100;
	printf("Concatenate Tests\n");

	r += concatenateTest(p, "ac",    "",  2, (char*[2]){"a", "c"});
	r += concatenateTest(p, "a,c",   ",", 2, (char*[2]){"a", "c"});
	r += concatenateTest(p, "a,b,c", ",", 3, (char*[3]){"a", "b", "c"});
	r += concatenateTest(p, "a",     "X", 1, (char*[1]){"a"});
	r += concatenateTest(p, "",      "",  0, NULL);

	if (pickle_delete(p) != PICKLE_OK)
		r = -10;

	printf("Concatenate Test: %s/%d\n", failed(r), r);

	return r;
}

static int picolTestEval(void) {
	static const struct test_t {
		int retcode;
		char *eval, *result;
	} ts[] = {
		{ PICKLE_OK,    "+  2 2",          "4"     },
		{ PICKLE_OK,    "* -2 9",          "-18"   },
		{ PICKLE_OK,    "join {a b c} ,",  "a,b,c" },
		{ PICKLE_ERROR, "return fail -1",  "fail"  },
	};

	printf("Evaluate Tests\n");
	int r = 0;
	for (size_t i = 0; i < sizeof(ts)/sizeof(ts[0]); i++)
		if (test(ts[i].eval, ts[i].result, ts[i].retcode) < 0)
			r = -(int)(i+1);
	printf("Evaluate Test: %s/%d\n", failed(r), r);
	return r;
}

/**@bug Use this test suite to investigate Line-Feed/Line-Number bug */
static int picolTestLineNumber(void) {
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
	printf("Line Number Tests\n");
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

		printf("%u: %d/%d\n", (unsigned)i, p->line, ts[i].line);

		if (pickle_delete(p) != PICKLE_OK)
			r = r ? r : -4001;
	}
	printf("Line Number Test: %s/%d\n", failed(r), r);
	return r;
}

static int picolTestGetSetVar(void) {
	long val = 0;
	int r = 0;
	pickle_t *p = NULL;

	printf("Get/Set Variable Tests\n");
	if (pickle_new(&p, NULL) != PICKLE_OK || !p)
		return -1;

	if (pickle_eval(p, "set a 54; set b 3; set c -4x") != PICKLE_OK)
		r = -1;

	if (pickle_get_var_integer(p, "a", &val) != PICKLE_OK || val != 54)
		r = -2;
	printf("a: %ld\n", val);

	if (pickle_get_var_integer(p, "c", &val) != PICKLE_OK || val != -4)
		r = -3;
	printf("c: %ld\n", val);

	printf("d: set d = 123\n");
	if (pickle_set_var_string(p, "d", "123") != PICKLE_OK)
		r = -4;

	if (pickle_get_var_integer(p, "d", &val) != PICKLE_OK || val != 123)
		r = -3;
	printf("d: %ld\n", val);

	if (pickle_delete(p) != PICKLE_OK)
		r = -4;
	printf("Get/Set Variable Test: %s/%d\n", failed(r), r);
	return r;
}

static int picolTestParser(void) { /**@todo The parser needs unit test writing for it */
	int r = 0;
	struct picolParser p = { NULL };

	static const char *token[] = {
		[PT_ESC] = "ESC", [PT_STR] = "STR", [PT_CMD] = "CMD",
		[PT_VAR] = "VAR", [PT_SEP] = "SEP", [PT_EOL] = "EOL",
		[PT_EOF] = "EOF",
	};

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

	printf("Parser Tests\n");
	for (size_t i = 0; i < sizeof(ts)/sizeof(ts[0]); i++) {
		const char *ch = NULL;
		int line = 1;
		picolParserInitialize(&p, ts[i].text, &line, &ch);
		printf("Parse {%s}\n", ts[i].text);
		do {
			if (picolGetToken(&p) == PICKLE_ERROR)
				break;
			assert(p.start && p.end);
			assert(p.type <= PT_EOF);
			printf("%d %d:%s: '%.*s'\n", line, p.type, token[p.type], (int)(p.end - p.start) + 1, p.start);
		} while(p.type != PT_EOF);
		printf("[END: %d]\n", line);
	}

	printf("Parser Test: %s/%d\n", failed(r), r);
	return r;
}

static int picolTestGetOpt(void) {
	pickle_getopt_t opt = { .init = 0 };

	char *argv[] = {
		"./program",
		"-h",
		"-f",
		"argument-to-f",
		"-c",
		"file",
	};
	const int argc = sizeof(argv) / sizeof(argv[0]);
	char *argument_to_f = NULL;
	int ch = 0, r = 0, result = 0;
	printf("GetOpt Tests\n");
	while ((ch = pickle_getopt(&opt, argc, argv, "hf:c")) != PICKLE_RETURN) {
		switch (ch) {
		case 'h': if (result & 1) r = -1; result |= 1; break;
		case 'f': if (result & 2) r = -2; result |= 2; argument_to_f = opt.arg; break;
		case 'c': if (result & 4) r = -4; result |= 4; break;
		default:
			r = -8;
		}
	}
	printf("argc: %d, result: %d\n", argc, result);
	r += result == 7 ? 0 : -8;
	if (argument_to_f)
		r += !strcmp("argument-to-f", argument_to_f) ? 0 : -16;
	else
		r += -32;
	printf("GetOpt Test: %s/%d\n", failed(r), r);
	return r;
}

int pickle_tests(void) {
	printf("Pickle Tests\n");
	typedef int (*test_func)(void);
	static const test_func ts[] = {
		picolTestSmallString,
		picolTestUnescape,
		picolTestConcat,
		picolTestEval,
		picolTestGetSetVar,
		picolTestLineNumber,
		picolTestParser,
		picolTestGetOpt,
	};
	int r = 0;
	for (size_t i = 0; i < sizeof(ts)/sizeof(ts[0]); i++)
		if (ts[i]() != 0)
			r = -1;
	printf("[DONE: %s]\n\n", r < 0 ? "FAIL" : "PASS");
	return r != 0 ? PICKLE_ERROR : PICKLE_OK;
}

#endif
