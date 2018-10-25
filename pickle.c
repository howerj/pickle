/**@file pickle.c
 * @brief Pickle: A tiny TCL like interpreter
 *
 *  This is a copy and extension of a tiny TCL interpreter called 'picol', by
 *  Antirez, under the BSD license, available at:
 *
 * <http://oldblog.antirez.com/post/picol.html>
 * <http://antirez.com/picol/picol.c.txt>
 *
 * The BSD license has been moved to the header.
 *
 * Extensions/Changes by Richard James Howe, available at:
 * <https://github.com/howerj/pickle>
 *
 * Style and coding guide lines:
 *   - 'pickle_' and snake_case is used for exported functions/variables/types
 *   - 'picol_'  and camelCase  is used for internal functions/variables/types
 *   - Use asserts wherever you can for as many preconditions, postconditions
 *   and invariants that you can think of. */
#include "pickle.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdarg.h>

#ifdef NDEBUG
#define DEBUGGING (0)
#else
#define DEBUGGING (1)
#endif

#define VERSION                  (1989)
#define DEFINE_MATHS             (1)
#define DEFAULT_ALLOCATOR        (1)
#define UNUSED(X)                ((void)(X))
#define BUILD_BUG_ON(condition)  ((void)sizeof(char[1 - 2*!!(condition)]))
#define implies(P, Q)            assert(!(P) || (Q)) /* material implication, immaterial if NDEBUG define */
#define verify(X)                do { if (!(X)) { abort(); } } while (0)
#define MALLOC(I, SZ)      (assert((SZ) > 0), (I)->allocator.malloc((I)->allocator.arena, (SZ)))
#define REALLOC(I, P, SZ) ((I)->allocator.realloc((I)->allocator.arena, (P), (SZ)))
#define FREE(I, P)           ((I)->allocator.free((I)->allocator.arena, (P)))

enum { PT_ESC, PT_STR, PT_CMD, PT_VAR, PT_SEP, PT_EOL, PT_EOF };

struct picolParser {
	const char *text;  /* the program */
	const char *p;     /* current text position */
	int len;           /* remaining length */
	const char *start; /* token start */
	const char *end;   /* token end */
	int type;          /* token type, PT_... */
	int insidequote;   /* true if inside " " */
	int *line;         /* pointer to line number */
	const char **ch;   /* pointer to global test position */
};

typedef struct {
	const char *name;
	pickle_command_func_t func;
	void *data;
} pickle_register_command_t;

enum { PV_STRING, PV_SMALL_STRING, PV_LINK };

typedef union {
	char *ptr, small[sizeof(char*)];
} compact_string_t;

struct pickle_var { /* strings are stored as either pointers, or as 'small' strings */
	compact_string_t name;
	union {
		compact_string_t val;    /* value */
		struct pickle_var *link; /* link to another variable */
		/*intptr_t*/
	} data;
	struct pickle_var *next;
	unsigned type      :2; /* type of data; string (pointer/small), or link (NB. Could add number type) */
	unsigned smallname :1; /* if true, name is stored as small string */
};

struct pickle_command {
	/* If online help in the form of help strings were to be added, we
	 * could add another field for it here */
	char *name;
	pickle_command_func_t func;
	void *privdata;
	struct pickle_command *next;
};

struct pickle_call_frame {
	struct pickle_var *vars;
	struct pickle_call_frame *parent; /* parent is NULL at top level */
};

struct pickle_interpreter {
	pickle_allocator_t allocator;        /* custom allocator, if desired */
	const char *result;                  /* result of an evaluation */
	const char *ch;                      /* the current text position; set if line != 0 */
	struct pickle_call_frame *callframe; /* internal use only; call stack */
	struct pickle_command **table;       /* internal use only; hash table */
	long length;                         /* internal use only; buckets in hash table */
	int level;                           /* internal use only; level of nesting */
	int line;                            /* current line number */
	unsigned initialized   :1;           /* if true, interpreter is initialized and ready to use */
	unsigned static_result :1;           /* internal use only: if true, result should not be freed */
};

static char *string_empty = "", *string_oom = "Out Of Memory";

static inline void static_assertions(void) {
	BUILD_BUG_ON(PICKLE_MAX_STRING    < 128);
	BUILD_BUG_ON(PICKLE_MAX_RECURSION < 8);
	BUILD_BUG_ON(PICKLE_MAX_ARGS      < 8);
	BUILD_BUG_ON(PICKLE_OK != 0);
}

static char *picolStrdup(pickle_allocator_t *a, const char *s) {
	assert(a);
	assert(s);
	const size_t l = strlen(s); /* NB: could use 'strnlen(s, PICKLE_MAX_STRING)' */
	char *r = a->malloc(a->arena, l + 1);
	if (!r)
		return NULL;
	return memcpy(r, s, l + 1);
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
		if (!strcmp(s, np->name))
			return np; /* found */
	return NULL; /* not found */
}

/* From 'The C Programming Language'.
 * <https://stackoverflow.com/questions/4384359/> */
int pickle_register_command(pickle_t *i, const char *name, pickle_command_func_t func, void *privdata) {
	assert(i);
	assert(name);
	assert(func);
	struct pickle_command *np;
	if ((np = picolGetCommand(i, name)) == NULL) { /* not found */
		np = MALLOC(i, sizeof(*np));
		if (np == NULL || (np->name = picolStrdup(&i->allocator, name)) == NULL) {
			FREE(i, np);
			return pickle_error_out_of_memory(i);
		}
		const unsigned long hashval = hash(name, strlen(name)) % i->length;
		np->next = i->table[hashval];
		i->table[hashval] = np;
	} else { /* already there */
		return pickle_error(i, "'%s' already defined", name);
	}
	np->func = func;
	np->privdata = privdata;
	return PICKLE_OK;
}

static void advance(struct picolParser *p) {
	assert(p);
	if (*p->line/*0 disables line count*/ && *p->ch < p->p) {
		*p->ch = p->p;
		if (*p->p == '\n')
			(*p->line)++;
	}
	p->p++;
	p->len--;
	assert(p->len >= 0);
}

static inline void picolParserInitialize(struct picolParser *p, const char *text, int *line, const char **ch) {
	assert(p);
	assert(text);
	assert(line);
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
		advance(p);
	p->end  = p->p - 1;
	p->type = PT_SEP;
	return PICKLE_OK;
}

static inline int picolParseEol(struct picolParser *p) {
	assert(p);
	p->start = p->p;
	while (picolIsSpaceChar(*p->p) || *p->p == ';')
		advance(p);
	p->end  = p->p - 1;
	p->type = PT_EOL;
	return PICKLE_OK;
}

static inline int picolParseCommand(struct picolParser *p) {
	assert(p);
	advance(p);
	p->start = p->p;
	for (int level = 1, blevel = 0; p->len; advance(p)) {
		if (*p->p == '[' && blevel == 0) {
			level++;
		} else if (*p->p == ']' && blevel == 0) {
			if (!--level)
				break;
		} else if (*p->p == '\\') {
			advance(p);
		} else if (*p->p == '{') {
			blevel++;
		} else if (*p->p == '}') {
			if (blevel != 0)
				blevel--;
		}
	}
	p->end  = p->p - 1;
	p->type = PT_CMD;
	if (*p->p == ']')
		advance(p);
	return PICKLE_OK;
}

static inline int picolIsVarChar(const int ch) {
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
}

static inline int picolParseVar(struct picolParser *p) {
	assert(p);
	advance(p); /* skip the $ */
	p->start = p->p;
	for (;picolIsVarChar(*p->p);)
	       	advance(p);
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
	advance(p);
	p->start = p->p;
	for (int level = 1;;) {
		if (p->len >= 2 && *p->p == '\\') {
			advance(p);
		} else if (p->len == 0 || *p->p == '}') {
			level--;
			if (level == 0 || p->len == 0) {
				p->end  = p->p - 1;
				p->type = PT_STR;
				if (p->len)
					advance(p); /* Skip final closed brace */
				return PICKLE_OK;
			}
		} else if (*p->p == '{') {
			level++;
		}
		advance(p);
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
		advance(p);
	}
	p->start = p->p;
	for (;p->len;) {
		switch(*p->p) {
		case '\\':
			if (p->len >= 2)
				advance(p);
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
				advance(p);
				return PICKLE_OK;
			}
			break;
		}
		advance(p);
	}
	p->end = p->p - 1;
	p->type = PT_ESC;
	return PICKLE_OK;
}

static inline int picolParseComment(struct picolParser *p) {
	assert(p);
	while (p->len && *p->p != '\n')
		advance(p);
	return PICKLE_OK;
}

static int picolGetToken(struct picolParser *p) {
	assert(p);
	for (;p->len;) {
		switch(*p->p) {
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
		FREE(i, (char*)i->result);
}

int pickle_error_out_of_memory(pickle_t *i) {
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

/* NB. This would be more useful if it accepted a printf style format string */
int pickle_set_result_string(pickle_t *i, const char *s) {
	assert(i);
	assert(i->result);
	assert(s);
	char *r = picolStrdup(&i->allocator, s);
	if (r) {
		picolFreeResult(i);
		i->static_result = 0;
		i->result = r;
		return PICKLE_OK;
	}
	return pickle_error_out_of_memory(i);
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

int pickle_get_line_number(pickle_t *i, int *line) {
	assert(i);
	assert(line);
	*line = 0;
	if (!(i->line) || !(i->ch))
		return PICKLE_ERROR;
	*line = i->line;
	return PICKLE_OK;
}

int pickle_get_call_depth(pickle_t *i, int *depth) {
	assert(i);
	assert(depth);
	*depth = i->level;
	if (i->level > PICKLE_MAX_RECURSION)
		return PICKLE_ERROR;
	return PICKLE_OK;
}

static struct pickle_var *picolGetVar(pickle_t *i, const char *name, int link) {
	assert(i);
	assert(name);
	struct pickle_var *v = i->callframe->vars;
	while (v) {
		const char *n = v->smallname ? &v->name.small[0] : v->name.ptr;
		assert(n);
		if (!strcmp(n, name)) {
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
		FREE(i, v->name.ptr);
}

static void picolFreeVarVal(pickle_t *i, struct pickle_var *v) {
	assert(i);
	assert(v);
	if (v->type == PV_STRING)
		FREE(i, v->data.val.ptr);
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
		memset(v->data.val.small, 0, sizeof(v->data.val.small));
		strncat(v->data.val.small, val, sizeof(v->data.val.small) - 1);
		return 0;
	}
	v->type = PV_STRING;
	return (v->data.val.ptr = picolStrdup(&i->allocator, val)) ? 0 : -1;
}

static inline int picolSetVarName(pickle_t *i, struct pickle_var *v, const char *name) {
	assert(i);
	assert(v);
	assert(name);
	if (picolIsSmallString(name)) {
		v->smallname = 1;
		memset(v->name.small, 0, sizeof(v->name.small));
		strncat(v->name.small, name, sizeof(v->name.small) - 1);
		return 0;
	}
	v->smallname = 0;
	return (v->name.ptr = picolStrdup(&i->allocator, name)) ? 0 : -1;
}

int pickle_set_var_string(pickle_t *i, const char *name, const char *val) {
	assert(i);
	assert(name);
	assert(val);
	struct pickle_var *v = picolGetVar(i, name, 1);
	if (v) {
		picolFreeVarVal(i, v);
		if (picolSetVarString(i, v, val) < 0)
			return pickle_error_out_of_memory(i);
	} else {
		if (!(v = MALLOC(i, sizeof(*v))))
			return pickle_error_out_of_memory(i);
		const int r1 = picolSetVarName(i, v, name);
		const int r2 = picolSetVarString(i, v, val);
		if (r1 < 0 || r2 < 0) {
			picolFreeVarName(i, v);
			picolFreeVarVal(i, v);
			FREE(i, v);
			return pickle_error_out_of_memory(i);
		}
		v->next = i->callframe->vars;
		i->callframe->vars = v;
	}
	return PICKLE_OK;
}

static const char *picolGetVarVal(struct pickle_var *v) {
	assert(v);
	assert((v->type == PV_SMALL_STRING) || (v->type == PV_STRING));
	switch(v->type) {
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

int pickle_error(pickle_t *i, const char *fmt, ...) {
	assert(i);
	assert(fmt);
	size_t off = 0;
	char errbuf[PICKLE_MAX_STRING] = { 0 };
	if (i->line)
		off = snprintf(errbuf, sizeof(errbuf)/2, "line %d: ", i->line);
	assert(off < PICKLE_MAX_STRING);
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(errbuf + off, sizeof(errbuf) - off, fmt, ap);
	va_end(ap);
	(void)pickle_set_result_string(i, errbuf);
	return PICKLE_ERROR;
}

int pickle_set_result_integer(pickle_t *i, long result) {
	assert(i);
	char buffy[64];
	const int r = snprintf(buffy, sizeof(buffy), "%ld", result); (void)r;
	assert(r >= 0 && r <= (int)(sizeof buffy));
	return pickle_set_result_string(i, buffy);
}

int pickle_set_var_integer(pickle_t *i, const char *name, long r) {
	char v[64];
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
		FREE(i, argv[j]);
	FREE(i, argv);
}

static int picolUnEscape(char *inout) {
	assert(inout);
	int j, k, ch;
	char r[PICKLE_MAX_STRING];
	for (j = 0, k = 0; (ch = inout[j]); j++, k++) {
		if (ch == '\\') {
			j++;
			switch(inout[j]) {
			case '\\': r[k] = '\\'; break;
			case  'n': r[k] = '\n'; break;
			case  't': r[k] = '\t'; break;
			case  'r': r[k] = '\r'; break;
			case  '"': r[k] = '"';  break;
			case  '[': r[k] = '[';  break;
			case  ']': r[k] = ']';  break;
			case  'e': r[k] = 27;   break;
			case  'x': {
				if (!inout[j+1] || !inout[j+2])
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
	memcpy(inout, r, k+1);
	return k;
}

static int picolEval(pickle_t *i, const char *t) {
	assert(i);
	assert(i->initialized);
	assert(t);
	struct picolParser p;
	int retcode = PICKLE_OK, argc = 0;
	char **argv = NULL;
	if (picolSetResultEmpty(i) != PICKLE_OK)
		return PICKLE_ERROR;
	picolParserInitialize(&p, t, &i->line, &i->ch);
	int prevtype = p.type;
	for (;;) {
		if (picolGetToken(&p) != PICKLE_OK)
			return pickle_error(i, "parser error");
		if (p.type == PT_EOF)
			break;
		int tlen = p.end - p.start + 1;
		if (tlen < 0)
			tlen = 0;
		char *t = MALLOC(i, tlen + 1);
		if (!t) {
			retcode = pickle_error_out_of_memory(i);
			goto err;
		}
		memcpy(t, p.start, tlen);
		t[tlen] = '\0';
		if (p.type == PT_VAR) {
			struct pickle_var * const v = picolGetVar(i, t, 1);
			if (!v) {
				retcode = pickle_error(i, "No such variable '%s'", t);
				FREE(i, t);
				goto err;
			}
			FREE(i, t);
			if (!(t = picolStrdup(&i->allocator, picolGetVarVal(v)))) {
				retcode = pickle_error_out_of_memory(i);
				goto err;
			}
		} else if (p.type == PT_CMD) {
			retcode = picolEval(i, t);
			FREE(i, t);
			if (retcode != PICKLE_OK)
				goto err;
			if (!(t = picolStrdup(&i->allocator, i->result))) {
				retcode = pickle_error_out_of_memory(i);
				goto err;
			}
		} else if (p.type == PT_ESC) {
			if (picolUnEscape(t) < 0) {
				retcode = pickle_error(i, "Invalid escape sequence '%s'", t);
				FREE(i, t);
				goto err;
			}
		} else if (p.type == PT_SEP) {
			prevtype = p.type;
			FREE(i, t);
			continue;
		}

		if (p.type == PT_EOL) { /* We have a complete command + args. Call it! */
			struct pickle_command *c = NULL;
			FREE(i, t);
			prevtype = p.type;
			if (argc) {
				if ((c = picolGetCommand(i, argv[0])) == NULL) {
					retcode = pickle_error(i, "No such command '%s'", argv[0]);
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
			if (!(argv = REALLOC(i, argv, sizeof(char*)*(argc + 1)))) {
				argv = old;
				retcode = pickle_error_out_of_memory(i);
				goto err;
			}
			argv[argc] = t;
			t = NULL;
			argc++;
		} else { /* Interpolation */
			const int oldlen = strlen(argv[argc - 1]), tlen = strlen(t);
			char *arg = REALLOC(i, argv[argc - 1], oldlen + tlen + 1);
			if (!arg) {
				retcode = pickle_error_out_of_memory(i);
				FREE(i, t);
				goto err;
			}
			argv[argc - 1] = arg;
			memcpy(argv[argc - 1] + oldlen, t, tlen);
			argv[argc - 1][oldlen + tlen] = '\0';
		}
		FREE(i, t);
		prevtype = p.type;
	}
err:
	picolFreeArgList(i, argc, argv);
	return retcode;
}

int pickle_eval(pickle_t *i, const char *t) {
	assert(i);
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
		return picolStrdup(&i->allocator, "");
	if (argc > (int)PICKLE_MAX_ARGS)
		return NULL;
	const size_t jl = strlen(join);
	size_t ls[argc] /* NB! */, l = 0;
	for (int j = 0; j < argc; j++) {
		const size_t sz = strlen(argv[j]);
		ls[j] = sz;
		l += sz + jl;
	}
	if ((l + 1) >= PICKLE_MAX_STRING)
		return NULL;
	char r[PICKLE_MAX_STRING];
	l = 0;
	for (int j = 0; j < argc; j++) {
		memcpy(r + l, argv[j], ls[j]);
		l += ls[j];
		if (jl && (j + 1) < argc) {
			memcpy(r + l, join, jl);
			l += jl;
		}
	}
	r[l] = 0;
	return picolStrdup(&i->allocator, r);
}

int pickle_error_arity(pickle_t *i, const int expected, const int argc, char **argv) {
	assert(i);
	assert(argc >= 1);
	assert(argv);
	char *as = concatenate(i, " ", argc, argv);
	if (!as)
		return pickle_error_out_of_memory(i);
	const int r = pickle_error(i, "Wrong number of args for '%s' (expected %d)\nGot: %s", argv[0], expected - 1, as);
	FREE(i, as);
	return r;
}

static int picolCommandMathUnary(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2)
		return pickle_error_arity(i, 3, argc, argv);
	char *op = argv[0];
	long a = atol(argv[1]);
	if (op[0] == '!')             { a = !a; }
	else if (op[0] == '~')        { a = ~a; }
	else if (!strcmp(op, "abs"))  { a = a < 0 ? -a : a; }
	else if (!strcmp(op, "bool")) { a = !!a; }
	else return pickle_error(i, "Unknown operator %s", op);
	return pickle_set_result_integer(i, a);
}

static int picolCommandMath(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 3)
		return pickle_error_arity(i, 3, argc, argv);
	char *op = argv[0];
	long a = atol(argv[1]), b = atol(argv[2]), c = 0;
	if (op[0] == '+') c = a + b;
	else if (op[0] == '-') c = a - b;
	else if (op[0] == '*') c = a * b;
	else if (op[0] == '/') { if (b) { c = a / b; } else { return pickle_error(i, "Division by 0"); } }
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
	else if (!strcmp(op, "min")) c = a < b ? a : b;
	else if (!strcmp(op, "max")) c = a > b ? a : b;
	else return pickle_error(i, "Unknown operator %s", op);
	return pickle_set_result_integer(i, c);
}

static int picolCommandSet(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 3 && argc != 2)
		return pickle_error_arity(i, 3, argc, argv);
	if (argc == 2) {
		const char *r = NULL;
		const int retcode = pickle_get_var_string(i, argv[1], &r);
		if (retcode != PICKLE_OK || !r)
			return pickle_error(i, "No such variable: %s", argv[1]);
		return pickle_set_result_string(i, r);
	}
	if (pickle_set_var_string(i, argv[1], argv[2]) != PICKLE_OK)
		return PICKLE_ERROR;
	return pickle_set_result_string(i, argv[2]);
}

static int picolCommandCatch(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 3)
		return pickle_error_arity(i, 3, argc, argv);
	const int r = picolEval(i, argv[1]);
	return pickle_set_var_integer(i, argv[2], r);
}

static int picolCommandIf(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	int retcode = 0;
	if (argc != 3 && argc != 5)
		return pickle_error_arity(i, 5, argc, argv);
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
		return pickle_error_arity(i, 3, argc, argv);
	for (;;) {
		int retcode = picolEval(i, argv[1]);
		if (retcode != PICKLE_OK)
			return retcode;
		if (atol(i->result)) {
			if ((retcode = picolEval(i, argv[2])) == PICKLE_CONTINUE)
				continue;
			else if (retcode == PICKLE_OK)
				continue;
			else if (retcode == PICKLE_BREAK)
				return PICKLE_OK;
			else
				return retcode;
		} else {
			return PICKLE_OK;
		}
	}
}

static int picolCommandRetCodes(pickle_t *i, const int argc, char **argv, void *pd) {
	if (argc != 1)
		return pickle_error_arity(i, 1, argc, argv);
	if (pd == (char*)0)
		return PICKLE_BREAK;
	if (pd == (char*)1)
		return PICKLE_CONTINUE;
	return PICKLE_OK;
}

static void picolVarFree(pickle_t *i, struct pickle_var *v) {
	if (!v)
		return;
	picolFreeVarName(i, v);
	picolFreeVarVal(i, v);
	FREE(i, v);
}

static void picolDropCallFrame(pickle_t *i) {
	assert(i);
	struct pickle_call_frame *cf = i->callframe;
	assert(i->level >= 0);
	i->level--;
	if (cf) {
		struct pickle_var *v = cf->vars, *t = NULL;
		while (v) {
			assert(v != v->next); /* Cycle? */
			t = v->next;
			picolVarFree(i, v);
			v = t;
		}
		i->callframe = cf->parent;
	}
	FREE(i, cf);
}

static void picolDropAllCallFrames(pickle_t *i) {
	assert(i);
	while (i->callframe)
		picolDropCallFrame(i);
}

static int picolCommandCallProc(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (i->level > (int)PICKLE_MAX_RECURSION)
		return pickle_error(i, "Recursion limit exceed (%d)", PICKLE_MAX_RECURSION);
	char **x = pd, *alist = x[0], *body = x[1], *p = picolStrdup(&i->allocator, alist), *tofree = NULL;
	int arity = 0, errcode = PICKLE_OK;
	struct pickle_call_frame *cf = MALLOC(i, sizeof(*cf));
	if (!cf || !p) {
		FREE(i, p);
		FREE(i, cf);
		return pickle_error_out_of_memory(i);
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
			FREE(i, tofree);
			picolDropCallFrame(i);
			return pickle_error_out_of_memory(i);
		}
		p++;
	}
	FREE(i, tofree);
	if (arity != (argc - 1))
		goto arityerr;
	errcode = picolEval(i, body);
	if (errcode == PICKLE_RETURN)
		errcode = PICKLE_OK;
	picolDropCallFrame(i);
	return errcode;
arityerr:
	pickle_error(i, "Proc '%s' called with wrong arg num", argv[0]);
	picolDropCallFrame(i);
	return PICKLE_ERROR;
}

static int picolCommandProc(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 4)
		return pickle_error_arity(i, 4, argc, argv);
	char **procdata = MALLOC(i, sizeof(char*)*2);
	if (!procdata)
		return pickle_error_out_of_memory(i);
	procdata[0] = picolStrdup(&i->allocator, argv[2]); /* arguments list */
	procdata[1] = picolStrdup(&i->allocator, argv[3]); /* procedure body */
	if (!(procdata[0]) || !(procdata[1])) {
		FREE(i, procdata[0]);
		FREE(i, procdata[1]);
		FREE(i, procdata);
		return pickle_error_out_of_memory(i);
	}
	return pickle_register_command(i, argv[1], picolCommandCallProc, procdata);
}

static int picolCommandReturn(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 1 && argc != 2 && argc != 3)
		return pickle_error_arity(i, 3, argc, argv);
	int retcode = PICKLE_RETURN;
	if (argc == 3)
		retcode = atol(argv[2]);
	if (argc == 1)
		return picolSetResultEmpty(i);
	if (pickle_set_result_string(i, argv[1]) != PICKLE_OK)
		return PICKLE_ERROR;
	return retcode;
}

static int doJoin(pickle_t *i, const char *join, const int argc, char **argv) { /**@todo fix join, should apply to list */
	char *e = concatenate(i, join, argc, argv);
	if (!e)
		return pickle_error_out_of_memory(i);
	picolFreeResult(i);
	i->static_result = 0;
	i->result = e;
	return PICKLE_OK;
}

static int picolCommandConcat(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	return doJoin(i, " ", argc - 1, argv + 1);
}

static int picolCommandJoin(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc < 2)
		return pickle_error_arity(i, 2, argc, argv);
	return doJoin(i, argv[1], argc - 2, argv + 2);
}

static int picolCommandEval(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	int r = doJoin(i, " ", argc - 1, argv + 1);
	if (r == PICKLE_OK) {
		char *e = picolStrdup(&i->allocator, i->result);
		if (!e)
			return pickle_error_out_of_memory(i);
		r = picolEval(i, e);
		FREE(i, e);
	}
	return r;
}

static int picolSetLevel(pickle_t *i, const char *levelStr) {
	const int top = levelStr[0] == '#';
	int level = atol(top ? &levelStr[1] : levelStr);
	if (level < 0)
		return pickle_error(i, "Negative level passed to 'uplevel/upvar': %d", level);

	if (top) {
		if (level != 0)
			return pickle_error(i, "Only #0 supported for 'uplevel/upvar'");
		level = INT_MAX;
	}

	for (int j = 0; j < level && i->callframe->parent; j++) {
		assert(i->callframe != i->callframe->parent);
		i->callframe = i->callframe->parent;
	}

	return PICKLE_OK;
}

static int picolCommandUpVar(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 4)
		return pickle_error_arity(i, 4, argc, argv);

	struct pickle_call_frame *cf = i->callframe;
	int retcode = PICKLE_OK;
	if ((retcode = pickle_set_var_string(i, argv[3], "")) != PICKLE_OK) {
		pickle_error(i, "Variable '%s' already exists", argv[3]);
		goto end;
	}

	struct pickle_var *myVar = cf->vars, *otherVar = NULL;

	if ((retcode = picolSetLevel(i, argv[1])) != PICKLE_OK)
		goto end;
	if (!(otherVar = picolGetVar(i, argv[2], 1))) {
		if (pickle_set_var_string(i, argv[2], "") != PICKLE_OK)
			return pickle_error_out_of_memory(i);
		otherVar = i->callframe->vars;
	}
	myVar->type = PV_LINK;
	myVar->data.link = otherVar;
end:
	i->callframe = cf;
	return retcode;
}

static int picolCommandUpLevel(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc < 2)
		return pickle_error_arity(i, 2, argc, argv);
	struct pickle_call_frame *cf = i->callframe;
	int retcode = PICKLE_OK;
	if ((retcode = picolSetLevel(i, argv[1])) != PICKLE_OK) {
		goto end;
	} else {
		char *e = concatenate(i, " ", argc - 2, argv + 2);
		if (!e) {
			retcode = pickle_error_out_of_memory(i);
			goto end;
		}
		retcode = picolEval(i, e);
		FREE(i, e);
	}
end:
	i->callframe = cf;
	return retcode;
}

static int picolCommandUnSet(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2)
		return pickle_error_arity(i, 2, argc, argv);
	struct pickle_call_frame *cf = i->callframe;
	struct pickle_var *p = NULL, *deleteMe = picolGetVar(i, argv[1], 0/*NB!*/);
	if (!deleteMe)
		return pickle_error(i, "Cannot unset '%s', no such variable", argv[1]);

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
		return pickle_set_result_integer(i, r++);
	}
	if (argc == 2) {
		long j = 0;
		for (; j < i->length; j++) {
			struct pickle_command *c = i->table[j];
			for (; c; c = c->next)
				if (!strcmp(argv[1], c->name))
					goto done;
		}
	done:
		return pickle_set_result_integer(i, j);
	}

	if (argc != 3)
		return pickle_error_arity(i, 3, argc, argv);
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
		return pickle_error(i, "Invalid command index '%ld'", r);
	assert(c);
	const int defined = c->func == picolCommandCallProc;
	const char *rq = argv[1];
	if (!strcmp(rq, "args")) {
		if (!defined)
			return pickle_set_result_string(i, "built-in");
		char **procdata = c->privdata;
		return pickle_set_result_string(i, procdata[0]);
	} else if (!strcmp(rq, "body")) {
		if (!defined)
			return pickle_set_result_string(i, "built-in");
		char **procdata = c->privdata;
		return pickle_set_result_string(i, procdata[1]);
	} else if (!strcmp(rq, "name")) {
		return pickle_set_result_string(i, c->name);
	}
	return pickle_error(i, "Unknown command request '%s'", rq);
}

static int picolRegisterCoreCommands(pickle_t *i) {
	assert(i);
	/* NOTE: to save on memory we could do command lookup against this
	 * static table for built in commands, instead of registering them
	 * in the normal way */
	static const pickle_register_command_t commands[] = {
		{ "set",       picolCommandSet,       NULL },
		{ "if",        picolCommandIf,        NULL },
		{ "while",     picolCommandWhile,     NULL },
		{ "break",     picolCommandRetCodes,  (char*)0 },
		{ "continue",  picolCommandRetCodes,  (char*)1 },
		{ "proc",      picolCommandProc,      NULL },
		{ "return",    picolCommandReturn,    NULL },
		{ "uplevel",   picolCommandUpLevel,   NULL },
		{ "upvar",     picolCommandUpVar,     NULL },
		{ "unset",     picolCommandUnSet,     NULL },
		{ "concat",    picolCommandConcat,    NULL },
		{ "join",      picolCommandJoin,      NULL },
		{ "eval",      picolCommandEval,      NULL },
		{ "catch",     picolCommandCatch,     NULL },
		{ "command",   picolCommandCommand,   NULL },
	};
	if (DEFINE_MATHS) {
		static const char *unary[] = { "!", "~", "abs", "bool" };
		static const char *binary[] = {
			"+", "-", "*", "/", ">", ">=", "<", "<=", "==", "!=",
			"<<", ">>", "&", "|", "^", "min", "max"
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
			FREE(i, procdata[0]);
			FREE(i, procdata[1]);
		}
		FREE(i, procdata);
	}
	FREE(i, p->name);
	FREE(i, p);
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
	FREE(i, i->table);
	memset(i, 0, sizeof *i);
	return PICKLE_OK;
}

static int picolInitialize(pickle_t *i, const pickle_allocator_t *a) {
	static_assertions();
	assert(i);
	assert(a);
	/*assert(!(i->initialized));*/
	memset(i, 0, sizeof *i);
	i->initialized   = 1;
	i->allocator     = *a;
	i->callframe     = i->allocator.malloc(i->allocator.arena, sizeof(*i->callframe));
	i->result        = string_empty;
	i->static_result = 1;
	i->table         = MALLOC(i, PICKLE_MAX_STRING);

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
/**@todo The parser needs unit test writing for it */

static inline const char *failed(int n) { if (n == 0) return "PASS"; return "FAIL"; }

static int test(const char *eval, const char *result, int retcode) {
	assert(eval);
	assert(result);
	int r = 0, actual = 0;
	pickle_t *p = NULL;
	const int rc = pickle_new(&p, NULL);
	if (rc != PICKLE_OK || !p)
		return -1;
	if ((actual = picolEval(p, eval)) != retcode) { r = -2; goto end; }
	if (!(p->result))                               { r = -3; goto end; }
	if (strcmp(p->result, result))                  { r = -4; goto end; }
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
		memset(m, 0, sizeof m);
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
		if (strcmp(m, ts[i].res)) {
			printf("not equal: %s\n", m);
			r = -2;
			continue;
		}
		printf("ok\n");
	}
	printf("Unescape Test: %s/%d\n", failed(r), r);
	return r;
}

static int picolTestConcat(void) {
	int r = 0;
	pickle_t *p = NULL;
	if (pickle_new(&p, NULL) != PICKLE_OK || !p)
		return -100;
	char *f = NULL;
	printf("Concatenate Tests\n");

	if (!(f = concatenate(p, "", 2, (char*[2]){"a", "c"}))) { r = -1; }
	else { if (strcmp(f, "ac")) { r = -2; } FREE(p, f); }

	if (!(f = concatenate(p, ",", 2, (char*[2]){"a", "c"}))) { r = -3; }
	else { if (strcmp(f, "a,c")) { r = -4; } FREE(p, f); }

	if (!(f = concatenate(p, ",", 3, (char*[3]){"a", "b", "c"}))) { r = -5; }
	else { if (strcmp(f, "a,b,c")) { r = -6; } FREE(p, f); }

	if (!(f = concatenate(p, "X", 1, (char*[1]){"a"}))) { r = -7; }
	else { if (strcmp(f, "a")) { r = -8; } FREE(p, f); }

	if (!(f = concatenate(p, "", 0, NULL))) { r = -9; }
	else { if (strcmp(f, "")) { r = -10; } FREE(p, f); }

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
		{ PICKLE_OK,    "+  2 2",        "4"     },
		{ PICKLE_OK,    "* -2 9",        "-18"   },
		{ PICKLE_OK,    "join , a b c",  "a,b,c" },
		{ PICKLE_ERROR, "return fail 1", "fail"  },
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

	int r = 0, line = 0;
	printf("Line Number Tests\n");
	for (size_t i = 0; i < sizeof(ts)/sizeof(ts[0]); i++) {
		pickle_t *p = NULL;
		if (pickle_new(&p, NULL) != PICKLE_OK || !p) {
			r = r ? r : -1001;
			break;
		}
		if (pickle_eval(p, ts[i].eval) != PICKLE_OK)
			r = r ? r : -2001;

		if (pickle_get_line_number(p, &line) != PICKLE_OK)
			r = r ? r : -3001;

		if (line != ts[i].line)
			r = r ? r : -(int)(i+1);

		printf("%u: %d/%d\n", (unsigned)i, line, ts[i].line);

		if (pickle_delete(p) != PICKLE_OK)
			r = r ? r : -4001;
	}
	printf("Line Number Test: %s/%d\n", failed(r), r);
	return r;
}

int picolTestGetSetVar(void) {
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
	};
	int r = 0;
	for (size_t i = 0; i < sizeof(ts)/sizeof(ts[0]); i++)
		if (ts[i]() != 0)
			r = -1;
	printf("[DONE: %s]\n\n", r < 0 ? "FAIL" : "PASS");
	return r != 0 ? PICKLE_ERROR : PICKLE_OK;
}

#endif
