/* Regular Expression Engine
 *
 * Modified from:
 * https://www.cs.princeton.edu/courses/archive/spr09/cos333/beautiful.html 
 *
 * Supports: "^$.*+?" and escaping
 * Nice to have: hex escape sequences, case insensitivity option, work on
 * binary data. */

#include <assert.h>
#include <stddef.h>

#define MAX_RECURSE (128)

enum { /* watch out for those negatives */
	START   =  '^', ESC   =  '%', EOI  = '\0',
	END     = -'$', ANY   = -'.', MANY = -'*', 
	ATLEAST = -'+', MAYBE = -'?', 
};

typedef struct {
	const char *start;
	const char *end;
	int max, greedy;
} match_t;

static int matchstar(match_t *x, int depth, int c, const char *regexp, const char *text);

/* escape a character, or return an operator */
static int escape(const unsigned ch, const int esc) {
	switch (ch) {
	case -END: case -ANY: case -MANY: case -ATLEAST: case -MAYBE: 
		return esc ? ch : -ch;
	case 'a': return esc ? '\a' : 'a';
	case 'b': return esc ? '\b' : 'b';
	case 'e': return esc ?  27  : 'e';
	case 'f': return esc ? '\f' : 'f';
	case 'n': return esc ? '\n' : 'n';
	case 'r': return esc ? '\r' : 'r';
	case 't': return esc ? '\t' : 't';
	case 'v': return esc ? '\v' : 'v';
	case START: case ESC: case EOI: break;
	}
	return ch;
}

/* matchhere: search for regexp at beginning of text */
static int matchhere(match_t *x, const int depth, const char *regexp, const char *text) {
	assert(x);
	assert(regexp);
	assert(text);
	if (x->max && depth > x->max)
		return -1;
	int r1 = EOI, r2 = EOI;
again:
	r1 = escape(regexp[0], 0);
	if (r1 == EOI) {
		x->end = text;
		return 1;
	}
	if (r1 == ESC) {
		r1 = escape(regexp[1], 1);
		if (r1 == EOI)
			return -1;
		regexp++;
	}
	r2 = escape(regexp[1], 0);
	if (r2 == MAYBE) {
		const int is = (r1 == *text) || (r1 == ANY);
		regexp += 2, text += is;
		goto again;
	}
	if (r2 == ATLEAST) { /* no point in a non-greedy ATLEAST */
		if (r1 != *text && r1 != ANY)
			return 0;
		return matchstar(x, depth + 1, r1, regexp + 2, text + 1);
	}
	if (r2 == MANY)
		return matchstar(x, depth + 1, r1, regexp + 2, text);
	if (r1 == END && r2 == EOI)
		return *text == EOI;
	if (*text != EOI && (r1 == ANY || r1 == *text)) {
		regexp++, text++;
		goto again;
	}
	return 0;
}

/* matchstar: search for c*regexp at beginning of text */
static int matchstar(match_t *x, const int depth, const int c, const char *regexp, const char *text) {
	assert(x);
	assert(regexp);
	assert(text);
	if (x->max && depth > x->max)
		return -1;
	if (x->greedy) {
		const char *t = NULL;
		for (t = text; *t != EOI && (*t == c || c == ANY); t++)
			;
		do {
			const int m = matchhere(x, depth + 1, regexp, t);
			if (m)
				return m;
		} while (t-- > text);
		return 0;
	}
	/* lazy */	
	do {    /* a '*' matches zero or more instances */
		const int m = matchhere(x, depth + 1, regexp, text);
		if (m)
			return m;
	} while (*text != EOI && (*text++ == c || c == ANY));
	return 0;
}

/* matcher: search for regexp anywhere in text */
int matcher(match_t *x, const char *regexp, const char *text) {
	assert(x);
	assert(regexp);
	assert(text);
	x->start = NULL;
	x->end   = NULL;
	if (regexp[0] == START)
		return matchhere(x, 0, regexp + 1, text);
	do {    /* must look even if string is empty */
		const int m = matchhere(x, 0, regexp, text);
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

int match(const char *regexp, const char *text) {
	assert(regexp);
	assert(text);
	match_t x = { NULL, NULL, MAX_RECURSE, 0 };
	return matcher(&x, regexp, text);
}

int match_tests(void) {
	assert(1 == match("a", "bba"));
	assert(1 == match(".", "x"));
	assert(1 == match("%.", "."));
	assert(0 == match("%.", "x"));
	assert(0 == match(".", ""));
	assert(0 == match("a", "b"));
	assert(1 == match("^a*b$", "b"));
	assert(1 == match("a*b", "b"));
	assert(1 == match("a*b", "ab"));
	assert(1 == match("a*b", "aaaab"));
	assert(1 == match("a*b", "xaaaab"));
	assert(0 == match("^a*b", "xaaaab"));
	assert(1 == match("a*b", "xaaaabx"));
	assert(1 == match("a*b", "xaaaaxb"));
	assert(0 == match("a*b", "xaaaax"));
	assert(0 == match("a$", "ab"));
	assert(1 == match("a*", ""));
	assert(1 == match("a*", "a"));
	assert(1 == match("a*", "aa"));
	assert(1 == match("a+", "a"));
	assert(0 == match("a+", ""));
	assert(1 == match("ca?b", "cab"));
	assert(1 == match("ca?b", "cb"));
	return 0;
}

#include <stdio.h>

int main(int argc, char **argv) {
	match_tests();

	if (argc == 1)
		return 0;

	if (argc != 3) {
		fprintf(stderr, "usage %s regex string\n", argv[0]);
		return 1;
	}
	match_t x = { NULL, NULL, MAX_RECURSE, 1 };
	const int m = matcher(&x, argv[1], argv[2]);
	char *d[] = { "error", "no match", "match" };
	char *s = d[m + 1];
	fprintf(stdout, "%s\n", s);
	if (m > 0) {
		assert(x.start);
		assert(x.end);
		int l = x.end - x.start;
		fprintf(stdout, "'%.*s'\n", l, x.start);
	}

	return m;
}
