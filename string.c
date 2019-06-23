#include "pickle.h"
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define UNUSED(X) ((void)(X))

static inline int compare(const char *a, const char *b) {
        return pickle_strcmp(a, b);
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

int pickle_command_string(pickle_t *i, const int argc, char **argv, void *pd) { /* Big! */
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
			return pickle_set_result_integer(i, pickle_hash_string(arg1, strlen(arg1)));
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
			const int r = match(arg1, arg2, PICKLE_MAX_RECURSION);
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
				return pickle_set_result_error_memory(i);
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
				return pickle_set_result_empty(i);
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
				return pickle_set_result_empty(i);
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

