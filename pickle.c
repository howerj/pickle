/* Pickle: A tiny TCL like interpreter
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
 * @todo Allocation changing; custom allocator should be an option, and
 * the allocator running out of memory should be dealt with, as well as
 * proper realloc handling.
 * @todo Better I/O
 * @todo Regexes, Split, Substring, more string operations
 */
#include "pickle.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNUSED(X) ((void)(X))

enum { PICOL_OK, PICOL_ERR, PICOL_RETURN, PICOL_BREAK, PICOL_CONTINUE };
enum { PT_ESC, PT_STR, PT_CMD, PT_VAR, PT_SEP, PT_EOL, PT_EOF };

struct picolParser {
    char *text;
    char *p;         /* current text position */
    int len;         /* remaining length */
    char *start;     /* token start */
    char *end;       /* token end */
    int type;        /* token type, PT_... */
    int insidequote; /* True if inside " " */
};

struct picolVar {
    char *name, *val;
    struct picolVar *next;
};

struct picolCmd {
    char *name;
    pickle_command_func_t func;
    void *privdata;
    struct picolCmd *next;
};

struct picolCallFrame {
    struct picolVar *vars;
    struct picolCallFrame *parent; /* parent is NULL at top level */
};

static char *picolStrdup(const char *s) {
	assert(s);
	const size_t l = strlen(s);
	char *r = malloc(l + 1);
	if(!r) 
		return NULL;
	return memcpy(r, s, l + 1);
}

static void advance(struct picolParser *p) {
	assert(p);
	p->p++; 
	p->len--;
}

static void picolInitParser(struct picolParser *p, char *text) {
	assert(p);
	assert(text);
	p->text = p->p = text;
	p->len = strlen(text);
	p->start = 0; p->end = 0; p->insidequote = 0;
	p->type = PT_EOL;
}

static int picolParseSep(struct picolParser *p) {
	assert(p);
	p->start = p->p;
	while(*p->p == ' ' || *p->p == '\t' || *p->p == '\n' || *p->p == '\r')
		advance(p);
	p->end = p->p - 1;
	p->type = PT_SEP;
	return PICOL_OK;
}

static int picolParseEol(struct picolParser *p) {
	assert(p);
	p->start = p->p;
	while(*p->p == ' ' || *p->p == '\t' || *p->p == '\n' || *p->p == '\r' || *p->p == ';')
		advance(p);
	p->end = p->p - 1;
	p->type = PT_EOL;
	return PICOL_OK;
}

static int picolParseCommand(struct picolParser *p) {
	assert(p);
	p->start = ++p->p; 
	p->len--;
	for(int level = 1, blevel = 0; p->len; advance(p)) {
		if (*p->p == '[' && blevel == 0) {
			level++;
		} else if (*p->p == ']' && blevel == 0) {
			if (!--level) break;
		} else if (*p->p == '\\') {
			advance(p);
		} else if (*p->p == '{') {
			blevel++;
		} else if (*p->p == '}') {
			if (blevel != 0) blevel--;
		}
	}
	p->end = p->p - 1;
	p->type = PT_CMD;
	if (*p->p == ']')
		advance(p);
	return PICOL_OK;
}

static inline int picolIsVarChar(int ch) {
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
}

static int picolParseVar(struct picolParser *p) {
	assert(p);
	advance(p); /* skip the $ */
	p->start = p->p; 
	for(;picolIsVarChar(*p->p);) 
	       	advance(p);
	if (p->start == p->p) { /* It's just a single char string "$" */
		p->start = p->end = p->p - 1;
		p->type = PT_STR;
	} else {
		p->end = p->p - 1;
		p->type = PT_VAR;
	}
	return PICOL_OK;
}

static int picolParseBrace(struct picolParser *p) {
	assert(p);
	int level = 1;
	p->start = ++p->p; p->len--;
	for(;;) {
		if (p->len >= 2 && *p->p == '\\') {
			advance(p);
		} else if (p->len == 0 || *p->p == '}') {
			level--;
			if (level == 0 || p->len == 0) {
				p->end = p->p - 1;
				if (p->len)
					advance(p); /* Skip final closed brace */
				p->type = PT_STR;
				return PICOL_OK;
			}
		} else if (*p->p == '{')
			level++;
		advance(p);
	}
	return PICOL_OK; /* unreached */
}

static int picolParseString(struct picolParser *p) {
	assert(p);
	int newword = (p->type == PT_SEP || p->type == PT_EOL || p->type == PT_STR);
	if (newword && *p->p == '{') 
		return picolParseBrace(p);
	else if (newword && *p->p == '"') {
		p->insidequote = 1;
		advance(p);
	}
	p->start = p->p;
	for(;;) {
		if (p->len == 0) {
			p->end = p->p - 1;
			p->type = PT_ESC;
			return PICOL_OK;
		}
		switch(*p->p) {
		case '\\':
			if (p->len >= 2)
				advance(p);
			break;
		case '$': case '[':
			p->end = p->p - 1;
			p->type = PT_ESC;
			return PICOL_OK;
		case ' ': case '\t': case '\n': case '\r': case ';':
			if (!p->insidequote) {
				p->end = p->p - 1;
				p->type = PT_ESC;
				return PICOL_OK;
			}
			break;
		case '"':
			if (p->insidequote) {
				p->end = p->p - 1;
				p->type = PT_ESC;
				advance(p);
				p->insidequote = 0;
				return PICOL_OK;
			}
			break;
		}
		advance(p);
	}
	return PICOL_OK; /* unreached */
}

static int picolParseComment(struct picolParser *p) {
	assert(p);
	while(p->len && *p->p != '\n')
		advance(p);
	return PICOL_OK;
}

static int picolGetToken(struct picolParser *p) {
	assert(p);
	for(;;) {
		if (!p->len) {
			if (p->type != PT_EOL && p->type != PT_EOF)
				p->type = PT_EOL;
			else
				p->type = PT_EOF;
			return PICOL_OK;
		}
		switch(*p->p) {
		case ' ': case '\t': case '\r':
			if (p->insidequote) 
				return picolParseString(p);
			return picolParseSep(p);
		case '\n': case ';':
			if (p->insidequote) 
				return picolParseString(p);
			return picolParseEol(p);
		case '[':
			return picolParseCommand(p);
		case '$':
			return picolParseVar(p);
		case '#':
			if (p->type == PT_EOL) {
				picolParseComment(p);
				continue;
			}
			return picolParseString(p);
		default:
			return picolParseString(p);
		}
	}
	return PICOL_OK; /* unreached */
}

static char *picolSetResult(pickle_t *i, const char *s) {
	assert(i);
	assert(s);
	free(i->result);
	return i->result = picolStrdup(s);
}

static struct picolVar *picolGetVar(pickle_t *i, const char *name) {
	assert(i);
	assert(name);
	struct picolVar *v = i->callframe->vars;
	while(v) {
		if (strcmp(v->name, name) == 0) 
			return v;
		v = v->next;
	}
	return NULL;
}

static int picolSetVar(pickle_t *i, char *name, char *val) {
	assert(i);
	assert(name);
	assert(val);
	struct picolVar *v = picolGetVar(i, name);
	if (v) {
		free(v->val);
		v->val = picolStrdup(val);
	} else {
		v = malloc(sizeof(*v));
		v->name = picolStrdup(name);
		v->val = picolStrdup(val);
		v->next = i->callframe->vars;
		i->callframe->vars = v;
	}
	return PICOL_OK;
}

static struct picolCmd *picolGetCommand(pickle_t *i, const char *name) {
	assert(i);
	assert(name);
	struct picolCmd *c = i->commands;
	while(c) {
		if (strcmp(c->name, name) == 0) 
			return c;
		c = c->next;
	}
	return NULL;
}

int pickle_register_command(pickle_t *i, const char *name, pickle_command_func_t f, void *privdata) {
	assert(i);
	assert(name);
	assert(f);
	struct picolCmd *c = picolGetCommand(i, name);
	if (c) {
		char errbuf[1024];
		snprintf(errbuf, sizeof errbuf, "Command '%s' already defined", name);
		picolSetResult(i, errbuf);
		return PICOL_ERR;
	}
	c = malloc(sizeof(*c));
	c->name = picolStrdup(name);
	c->func = f;
	c->privdata = privdata;
	c->next = i->commands;
	i->commands = c;
	return PICOL_OK;
}

int pickle_eval(pickle_t *i, char *t) {
	assert(i);
	assert(i->initialized);
	assert(t);
	struct picolParser p;
	int argc = 0;
	char **argv = NULL;
	char errbuf[1024];
	int retcode = PICOL_OK;
	picolSetResult(i, "");
	picolInitParser(&p, t);
	for(;;) {
		int prevtype = p.type;
		picolGetToken(&p);
		if (p.type == PT_EOF) 
			break;
		int tlen = p.end-p.start+1;
		if (tlen < 0) 
			tlen = 0;
		char *t = malloc(tlen+1);
		memcpy(t, p.start, tlen);
		t[tlen] = '\0';
		if (p.type == PT_VAR) {
			struct picolVar *v = picolGetVar(i, t);
			if (!v) {
				snprintf(errbuf, 1024, "No such variable '%s'", t);
				free(t);
				picolSetResult(i, errbuf);
				retcode = PICOL_ERR;
				goto err;
			}
			free(t);
			t = picolStrdup(v->val);
		} else if (p.type == PT_CMD) {
			retcode = pickle_eval(i, t);
			free(t);
			if (retcode != PICOL_OK) 
				goto err;
			t = picolStrdup(i->result);
		} else if (p.type == PT_ESC) {
			/* XXX: escape handling missing! */
		} else if (p.type == PT_SEP) {
			prevtype = p.type;
			free(t);
			continue;
		}
		/* We have a complete command + args. Call it! */
		if (p.type == PT_EOL) {
			struct picolCmd *c = NULL;
			free(t);
			prevtype = p.type;
			if (argc) {
				if ((c = picolGetCommand(i, argv[0])) == NULL) {
					snprintf(errbuf, 1024, "No such command '%s'", argv[0]);
					picolSetResult(i, errbuf);
					retcode = PICOL_ERR;
					goto err;
				}
				retcode = c->func(i, argc, argv, c->privdata);
				if (retcode != PICOL_OK) 
					goto err;
			}
			/* Prepare for the next command */
			for (int j = 0; j < argc; j++) 
				free(argv[j]);
			free(argv);
			argv = NULL;
			argc = 0;
			continue;
		}
		/* We have a new token, append to the previous or as new arg? */
		if (prevtype == PT_SEP || prevtype == PT_EOL) {
			argv = realloc(argv, sizeof(char*)*(argc+1));
			argv[argc] = t;
			argc++;
		} else { /* Interpolation */
			const int oldlen = strlen(argv[argc - 1]), tlen = strlen(t);
			argv[argc - 1] = realloc(argv[argc - 1], oldlen+tlen+1);
			memcpy(argv[argc - 1]+oldlen, t, tlen);
			argv[argc - 1][oldlen+tlen]='\0';
			free(t);
		}
		prevtype = p.type;
	}
err:
	for (int j = 0; j < argc; j++) 
		free(argv[j]);
	free(argv);
	return retcode;
}

static int picolArityErr(pickle_t *i, const char *name) {
	assert(i);
	assert(name);
	char errbuf[1024];
	snprintf(errbuf, sizeof errbuf, "Wrong number of args for %s", name);
	picolSetResult(i, errbuf);
	return PICOL_ERR;
}

static int picolCommandMath(pickle_t *i, int argc, char **argv, void *pd) {
	assert(i);
	assert(argv);
	UNUSED(pd);
	if (argc != 3) 
		return picolArityErr(i, argv[0]);
	char buf[64]; 
	int a = atoi(argv[1]), b = atoi(argv[2]), c = 0;
	if (argv[0][0] == '+') c = a + b;
	else if (argv[0][0] == '-') c = a - b;
	else if (argv[0][0] == '*') c = a * b;
	else if (argv[0][0] == '/') c = a / b;
	else if (argv[0][0] == '>' && argv[0][1] == '\0') c = a >  b;
	else if (argv[0][0] == '>' && argv[0][1] == '=')  c = a >= b;
	else if (argv[0][0] == '<' && argv[0][1] == '\0') c = a <  b;
	else if (argv[0][0] == '<' && argv[0][1] == '=')  c = a <= b;
	else if (argv[0][0] == '=' && argv[0][1] == '=')  c = a == b;
	else if (argv[0][0] == '!' && argv[0][1] == '=')  c = a != b;
	else exit(-1);
	snprintf(buf, 64, "%d", c);
	picolSetResult(i, buf);
	return PICOL_OK;
}

static int picolCommandSet(pickle_t *i, int argc, char **argv, void *pd) {
	assert(i);
	assert(argv);
	UNUSED(pd);
	if (argc != 3) 
		return picolArityErr(i, argv[0]);
	picolSetVar(i, argv[1], argv[2]);
	picolSetResult(i, argv[2]);
	return PICOL_OK;
}

static int picolCommandPuts(pickle_t *i, int argc, char **argv, void *pd) {
	assert(i);
	assert(argv);
	UNUSED(pd);
	if (argc != 2) 
		return picolArityErr(i, argv[0]);
	printf("%s\n", argv[1]);
	return PICOL_OK;
}

static int picolCommandIf(pickle_t *i, int argc, char **argv, void *pd) {
	assert(i);
	assert(argv);
	UNUSED(pd);
	int retcode = 0;
	if (argc != 3 && argc != 5) 
		return picolArityErr(i, argv[0]);
	if ((retcode = pickle_eval(i, argv[1])) != PICOL_OK) 
		return retcode;
	if (atoi(i->result)) 
		return pickle_eval(i, argv[2]);
	else if (argc == 5) 
		return pickle_eval(i, argv[4]);
	return PICOL_OK;
}

static int picolCommandWhile(pickle_t *i, int argc, char **argv, void *pd) {
	assert(i);
	assert(argv);
	UNUSED(pd);
	if (argc != 3) 
		return picolArityErr(i, argv[0]);
	for(;;) {
		int retcode = pickle_eval(i, argv[1]);
		if (retcode != PICOL_OK) 
			return retcode;
		if (atoi(i->result)) {
			if ((retcode = pickle_eval(i, argv[2])) == PICOL_CONTINUE) 
				continue;
			else if (retcode == PICOL_OK) 
				continue;
			else if (retcode == PICOL_BREAK) 
				return PICOL_OK;
			else 
				return retcode;
		} else {
			return PICOL_OK;
		}
	}
}

static int picolCommandRetCodes(pickle_t *i, int argc, char **argv, void *pd) {
	assert(i);
	assert(argv);
	UNUSED(pd);
	if (argc != 1) 
		return picolArityErr(i, argv[0]);
	if (strcmp(argv[0], "break") == 0) 
		return PICOL_BREAK;
	else if (strcmp(argv[0], "continue") == 0) 
		return PICOL_CONTINUE;
	return PICOL_OK;
}

static void picolDropCallFrame(pickle_t *i) {
	assert(i);
	struct picolCallFrame *cf = i->callframe;
	struct picolVar *v = cf->vars, *t = NULL;
	while(v) {
		t = v->next;
		free(v->name);
		free(v->val);
		free(v);
		v = t;
	}
	i->callframe = cf->parent;
	free(cf);
}

static int picolCommandCallProc(pickle_t *i, int argc, char **argv, void *pd) {
	char **x = pd, *alist = x[0], *body = x[1], *p = picolStrdup(alist), *tofree = NULL;
	struct picolCallFrame *cf = malloc(sizeof(*cf));
	int arity = 0, done = 0, errcode = PICOL_OK;
	char errbuf[1024];
	cf->vars = NULL;
	cf->parent = i->callframe;
	i->callframe = cf;
	tofree = p;
	for(;;) {
		char *start = p;
		while(*p != ' ' && *p != '\0') p++;
		if (*p != '\0' && p == start) {
			p++; 
			continue;
		}
		if (p == start) 
			break;
		if (*p == '\0') 
			done = 1; 
		else *p = '\0';
		if (++arity > (argc - 1)) 
			goto arityerr;
		picolSetVar(i, start, argv[arity]);
		p++;
		if (done) 
			break;
	}
	free(tofree);
	if (arity != argc - 1) 
		goto arityerr;
	errcode = pickle_eval(i, body);
	if (errcode == PICOL_RETURN) errcode = PICOL_OK;
	picolDropCallFrame(i); /* remove the called proc callframe */
	return errcode;
arityerr:
	snprintf(errbuf, 1024, "Proc '%s' called with wrong arg num", argv[0]);
	picolSetResult(i, errbuf);
	picolDropCallFrame(i); /* remove the called proc callframe */
	return PICOL_ERR;
}

static int picolCommandProc(pickle_t *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	char **procdata = malloc(sizeof(char*)*2);
	if (argc != 4) 
		return picolArityErr(i, argv[0]);
	procdata[0] = picolStrdup(argv[2]); /* arguments list */
	procdata[1] = picolStrdup(argv[3]); /* procedure body */
	return pickle_register_command(i, argv[1], picolCommandCallProc, procdata);
}

static int picolCommandReturn(pickle_t *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 1 && argc != 2) 
		return picolArityErr(i, argv[0]);
	picolSetResult(i, (argc == 2) ? argv[1] : "");
	return PICOL_RETURN;
}

void picolRegisterCoreCommands(pickle_t *i) {
	assert(i);
	static const char *name[] = { "+", "-", "*", "/", ">", ">=", "<", "<=", "==", "!=" };
	for (size_t j = 0; j < sizeof(name)/sizeof(char*); j++)
		pickle_register_command(i, name[j], picolCommandMath, NULL);
	pickle_register_command(i,  "set",       picolCommandSet,       NULL);
	pickle_register_command(i,  "puts",      picolCommandPuts,      NULL/* stdout, stderr */);
	pickle_register_command(i,  "if",        picolCommandIf,        NULL);
	pickle_register_command(i,  "while",     picolCommandWhile,     NULL);
	pickle_register_command(i,  "break",     picolCommandRetCodes,  NULL);
	pickle_register_command(i,  "continue",  picolCommandRetCodes,  NULL);
	pickle_register_command(i,  "proc",      picolCommandProc,      NULL);
	pickle_register_command(i,  "return",    picolCommandReturn,    NULL);
}

int pickle_initialize(pickle_t *i) {
	assert(i);
	assert(!(i->initialized));
	i->level             = 0;
	i->callframe         = malloc(sizeof(struct picolCallFrame));
	i->callframe->vars   = NULL;
	i->callframe->parent = NULL;
	i->commands          = NULL;
	i->result            = picolStrdup(""); /* intern common strings? */
	picolRegisterCoreCommands(i);
	i->initialized = 1;
	return 0;
}

static void pickleFreeCmd(struct picolCmd *p) {
	if(p) {
		free(p->name);
		if(p->func == picolCommandCallProc) {
			char **procdata = (char**) p->privdata;
			if(procdata) {
				free(procdata[0]);
				free(procdata[1]);
			}
			free(procdata);
		}
	}
	free(p);
}

int pickle_deinitialize(pickle_t *i) {
	assert(i);
	i->initialized = 0;
	picolDropCallFrame(i);
	free(i->result);
	struct picolCmd *c = i->commands, *p = NULL;
	for(; c; p = c, c = c->next)
		pickleFreeCmd(p);
	pickleFreeCmd(p);
	return 0;
}

