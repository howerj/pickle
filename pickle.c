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
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pickle.h"

#define UNUSED(X) ((void)(X))

enum {PT_ESC,PT_STR,PT_CMD,PT_VAR,PT_SEP,PT_EOL,PT_EOF};

static char *pickle_strdup(const char *s) {
	assert(s);
	const size_t l = strlen(s);
	char *r = malloc(l + 1);
	if(!r) return NULL;
	return memcpy(r, s, l + 1);
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
	p->start = p->p;
	while(*p->p == ' ' || *p->p == '\t' || *p->p == '\n' || *p->p == '\r') {
		p->p++; p->len--;
	}
	p->end = p->p-1;
	p->type = PT_SEP;
	return PICOL_OK;
}

static int picolParseEol(struct picolParser *p) {
	p->start = p->p;
	while(*p->p == ' ' || *p->p == '\t' || *p->p == '\n' || *p->p == '\r' ||
		  *p->p == ';')
	{
		p->p++; p->len--;
	}
	p->end = p->p-1;
	p->type = PT_EOL;
	return PICOL_OK;
}

static int picolParseCommand(struct picolParser *p) {
	int level = 1;
	int blevel = 0;
	p->start = ++p->p; p->len--;
	while (1) {
		if (p->len == 0) {
			break;
		} else if (*p->p == '[' && blevel == 0) {
			level++;
		} else if (*p->p == ']' && blevel == 0) {
			if (!--level) break;
		} else if (*p->p == '\\') {
			p->p++; p->len--;
		} else if (*p->p == '{') {
			blevel++;
		} else if (*p->p == '}') {
			if (blevel != 0) blevel--;
		}
		p->p++; p->len--;
	}
	p->end = p->p-1;
	p->type = PT_CMD;
	if (*p->p == ']') {
		p->p++; p->len--;
	}
	return PICOL_OK;
}

static int picolParseVar(struct picolParser *p) {
	p->start = ++p->p; p->len--; /* skip the $ */
	for(;;) {
		if ((*p->p >= 'a' && *p->p <= 'z') || (*p->p >= 'A' && *p->p <= 'Z') ||
			(*p->p >= '0' && *p->p <= '9') || *p->p == '_')
		{
			p->p++; p->len--; continue;
		}
		break;
	}
	if (p->start == p->p) { /* It's just a single char string "$" */
		p->start = p->end = p->p-1;
		p->type = PT_STR;
	} else {
		p->end = p->p-1;
		p->type = PT_VAR;
	}
	return PICOL_OK;
}

static int picolParseBrace(struct picolParser *p) {
	int level = 1;
	p->start = ++p->p; p->len--;
	for(;;) {
		if (p->len >= 2 && *p->p == '\\') {
			p->p++; p->len--;
		} else if (p->len == 0 || *p->p == '}') {
			level--;
			if (level == 0 || p->len == 0) {
				p->end = p->p-1;
				if (p->len) {
					p->p++; p->len--; /* Skip final closed brace */
				}
				p->type = PT_STR;
				return PICOL_OK;
			}
		} else if (*p->p == '{')
			level++;
		p->p++; p->len--;
	}
	return PICOL_OK; /* unreached */
}

static int picolParseString(struct picolParser *p) {
	int newword = (p->type == PT_SEP || p->type == PT_EOL || p->type == PT_STR);
	if (newword && *p->p == '{') return picolParseBrace(p);
	else if (newword && *p->p == '"') {
		p->insidequote = 1;
		p->p++; p->len--;
	}
	p->start = p->p;
	for(;;) {
		if (p->len == 0) {
			p->end = p->p-1;
			p->type = PT_ESC;
			return PICOL_OK;
		}
		switch(*p->p) {
		case '\\':
			if (p->len >= 2) {
				p->p++; p->len--;
			}
			break;
		case '$': case '[':
			p->end = p->p-1;
			p->type = PT_ESC;
			return PICOL_OK;
		case ' ': case '\t': case '\n': case '\r': case ';':
			if (!p->insidequote) {
				p->end = p->p-1;
				p->type = PT_ESC;
				return PICOL_OK;
			}
			break;
		case '"':
			if (p->insidequote) {
				p->end = p->p-1;
				p->type = PT_ESC;
				p->p++; p->len--;
				p->insidequote = 0;
				return PICOL_OK;
			}
			break;
		}
		p->p++; p->len--;
	}
	return PICOL_OK; /* unreached */
}

static int picolParseComment(struct picolParser *p) {
	while(p->len && *p->p != '\n') {
		p->p++; p->len--;
	}
	return PICOL_OK;
}

static int picolGetToken(struct picolParser *p) {
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
			if (p->insidequote) return picolParseString(p);
			return picolParseSep(p);
		case '\n': case ';':
			if (p->insidequote) return picolParseString(p);
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

static void picolSetResult(struct picolInterp *i, char *s) {
	free(i->result);
	i->result = pickle_strdup(s);
}

static struct picolVar *picolGetVar(struct picolInterp *i, char *name) {
	struct picolVar *v = i->callframe->vars;
	while(v) {
		if (strcmp(v->name,name) == 0) return v;
		v = v->next;
	}
	return NULL;
}

static int picolSetVar(struct picolInterp *i, char *name, char *val) {
	struct picolVar *v = picolGetVar(i,name);
	if (v) {
		free(v->val);
		v->val = pickle_strdup(val);
	} else {
		v = malloc(sizeof(*v));
		v->name = pickle_strdup(name);
		v->val = pickle_strdup(val);
		v->next = i->callframe->vars;
		i->callframe->vars = v;
	}
	return PICOL_OK;
}

static struct picolCmd *picolGetCommand(struct picolInterp *i, const char *name) {
	struct picolCmd *c = i->commands;
	while(c) {
		if (strcmp(c->name, name) == 0) return c;
		c = c->next;
	}
	return NULL;
}

int picolRegisterCommand(struct picolInterp *i, const char *name, picolCmdFunc f, void *privdata) {
	struct picolCmd *c = picolGetCommand(i,name);
	char errbuf[1024];
	if (c) {
		snprintf(errbuf,1024,"Command '%s' already defined",name);
		picolSetResult(i,errbuf);
		return PICOL_ERR;
	}
	c = malloc(sizeof(*c));
	c->name = pickle_strdup(name);
	c->func = f;
	c->privdata = privdata;
	c->next = i->commands;
	i->commands = c;
	return PICOL_OK;
}

int picolEval(struct picolInterp *i, char *t) {
	assert(i);
	assert(t);
	struct picolParser p;
	int argc = 0, j;
	char **argv = NULL;
	char errbuf[1024];
	int retcode = PICOL_OK;
	picolSetResult(i,"");
	picolInitParser(&p,t);
	for(;;) {
		char *t;
		int tlen;
		int prevtype = p.type;
		picolGetToken(&p);
		if (p.type == PT_EOF) break;
		tlen = p.end-p.start+1;
		if (tlen < 0) tlen = 0;
		t = malloc(tlen+1);
		memcpy(t, p.start, tlen);
		t[tlen] = '\0';
		if (p.type == PT_VAR) {
			struct picolVar *v = picolGetVar(i,t);
			if (!v) {
				snprintf(errbuf,1024,"No such variable '%s'",t);
				free(t);
				picolSetResult(i,errbuf);
				retcode = PICOL_ERR;
				goto err;
			}
			free(t);
			t = pickle_strdup(v->val);
		} else if (p.type == PT_CMD) {
			retcode = picolEval(i,t);
			free(t);
			if (retcode != PICOL_OK) goto err;
			t = pickle_strdup(i->result);
		} else if (p.type == PT_ESC) {
			/* XXX: escape handling missing! */
		} else if (p.type == PT_SEP) {
			prevtype = p.type;
			free(t);
			continue;
		}
		/* We have a complete command + args. Call it! */
		if (p.type == PT_EOL) {
			struct picolCmd *c;
			free(t);
			prevtype = p.type;
			if (argc) {
				if ((c = picolGetCommand(i,argv[0])) == NULL) {
					snprintf(errbuf,1024,"No such command '%s'",argv[0]);
					picolSetResult(i,errbuf);
					retcode = PICOL_ERR;
					goto err;
				}
				retcode = c->func(i,argc,argv,c->privdata);
				if (retcode != PICOL_OK) goto err;
			}
			/* Prepare for the next command */
			for (j = 0; j < argc; j++) free(argv[j]);
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
			int oldlen = strlen(argv[argc-1]), tlen = strlen(t);
			argv[argc-1] = realloc(argv[argc-1], oldlen+tlen+1);
			memcpy(argv[argc-1]+oldlen, t, tlen);
			argv[argc-1][oldlen+tlen]='\0';
			free(t);
		}
		prevtype = p.type;
	}
err:
	for (j = 0; j < argc; j++) free(argv[j]);
	free(argv);
	return retcode;
}

static int picolArityErr(struct picolInterp *i, char *name) {
	char buf[1024];
	snprintf(buf,1024,"Wrong number of args for %s",name);
	picolSetResult(i,buf);
	return PICOL_ERR;
}

static int picolCommandMath(struct picolInterp *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	char buf[64]; int a, b, c;
	if (argc != 3) return picolArityErr(i,argv[0]);
	a = atoi(argv[1]); b = atoi(argv[2]);
	if (argv[0][0] == '+') c = a+b;
	else if (argv[0][0] == '-') c = a-b;
	else if (argv[0][0] == '*') c = a*b;
	else if (argv[0][0] == '/') c = a/b;
	else if (argv[0][0] == '>' && argv[0][1] == '\0') c = a > b;
	else if (argv[0][0] == '>' && argv[0][1] == '=') c = a >= b;
	else if (argv[0][0] == '<' && argv[0][1] == '\0') c = a < b;
	else if (argv[0][0] == '<' && argv[0][1] == '=') c = a <= b;
	else if (argv[0][0] == '=' && argv[0][1] == '=') c = a == b;
	else if (argv[0][0] == '!' && argv[0][1] == '=') c = a != b;
	else c = 0; /* I hate warnings */
	snprintf(buf,64,"%d",c);
	picolSetResult(i,buf);
	return PICOL_OK;
}

static int picolCommandSet(struct picolInterp *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 3) return picolArityErr(i,argv[0]);
	picolSetVar(i,argv[1],argv[2]);
	picolSetResult(i,argv[2]);
	return PICOL_OK;
}

static int picolCommandPuts(struct picolInterp *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2) return picolArityErr(i,argv[0]);
	printf("%s\n", argv[1]);
	return PICOL_OK;
}

static int picolCommandIf(struct picolInterp *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	int retcode;
	if (argc != 3 && argc != 5) return picolArityErr(i,argv[0]);
	if ((retcode = picolEval(i,argv[1])) != PICOL_OK) return retcode;
	if (atoi(i->result)) return picolEval(i,argv[2]);
	else if (argc == 5) return picolEval(i,argv[4]);
	return PICOL_OK;
}

static int picolCommandWhile(struct picolInterp *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 3) return picolArityErr(i,argv[0]);
	for(;;) {
		int retcode = picolEval(i,argv[1]);
		if (retcode != PICOL_OK) return retcode;
		if (atoi(i->result)) {
			if ((retcode = picolEval(i,argv[2])) == PICOL_CONTINUE) continue;
			else if (retcode == PICOL_OK) continue;
			else if (retcode == PICOL_BREAK) return PICOL_OK;
			else return retcode;
		} else {
			return PICOL_OK;
		}
	}
}

static int picolCommandRetCodes(struct picolInterp *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 1) return picolArityErr(i,argv[0]);
	if (strcmp(argv[0],"break") == 0) return PICOL_BREAK;
	else if (strcmp(argv[0],"continue") == 0) return PICOL_CONTINUE;
	return PICOL_OK;
}

static void picolDropCallFrame(struct picolInterp *i) {
	struct picolCallFrame *cf = i->callframe;
	struct picolVar *v = cf->vars, *t;
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

static int picolCommandCallProc(struct picolInterp *i, int argc, char **argv, void *pd) {
	char **x=pd, *alist=x[0], *body=x[1], *p=pickle_strdup(alist), *tofree;
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
			p++; continue;
		}
		if (p == start) break;
		if (*p == '\0') done=1; else *p = '\0';
		if (++arity > argc-1) goto arityerr;
		picolSetVar(i,start,argv[arity]);
		p++;
		if (done) break;
	}
	free(tofree);
	if (arity != argc-1) goto arityerr;
	errcode = picolEval(i,body);
	if (errcode == PICOL_RETURN) errcode = PICOL_OK;
	picolDropCallFrame(i); /* remove the called proc callframe */
	return errcode;
arityerr:
	snprintf(errbuf,1024,"Proc '%s' called with wrong arg num",argv[0]);
	picolSetResult(i,errbuf);
	picolDropCallFrame(i); /* remove the called proc callframe */
	return PICOL_ERR;
}

static int picolCommandProc(struct picolInterp *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	char **procdata = malloc(sizeof(char*)*2);
	if (argc != 4) return picolArityErr(i,argv[0]);
	procdata[0] = pickle_strdup(argv[2]); /* arguments list */
	procdata[1] = pickle_strdup(argv[3]); /* procedure body */
	return picolRegisterCommand(i,argv[1],picolCommandCallProc,procdata);
}

static int picolCommandReturn(struct picolInterp *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 1 && argc != 2) return picolArityErr(i,argv[0]);
	picolSetResult(i, (argc == 2) ? argv[1] : "");
	return PICOL_RETURN;
}

void picolRegisterCoreCommands(struct picolInterp *i) {
	assert(i);
	static const char *name[] = {"+","-","*","/",">",">=","<","<=","==","!="};
	for (size_t j = 0; j < sizeof(name)/sizeof(char*); j++)
		picolRegisterCommand(i,name[j],picolCommandMath,NULL);
	picolRegisterCommand(i,"set",picolCommandSet,NULL);
	picolRegisterCommand(i,"puts",picolCommandPuts,NULL);
	picolRegisterCommand(i,"if",picolCommandIf,NULL);
	picolRegisterCommand(i,"while",picolCommandWhile,NULL);
	picolRegisterCommand(i,"break",picolCommandRetCodes,NULL);
	picolRegisterCommand(i,"continue",picolCommandRetCodes,NULL);
	picolRegisterCommand(i,"proc",picolCommandProc,NULL);
	picolRegisterCommand(i,"return",picolCommandReturn,NULL);
}

void picolInitInterp(struct picolInterp *i) {
	assert(i);
	i->level = 0;
	i->callframe = malloc(sizeof(struct picolCallFrame));
	i->callframe->vars = NULL;
	i->callframe->parent = NULL;
	i->commands = NULL;
	i->result = pickle_strdup("");
}


