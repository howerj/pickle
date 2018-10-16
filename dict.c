/* https://stackoverflow.com/questions/4384359/
 * The C Programming language
 *
 * @todo integrate this small dictionary library into 'pickle.c',
 * replacing the (slow) command lookup */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

struct nlist { /* table entry: */
	struct nlist *next; /* next entry in chain */
	char *name; /* defined name */
	char *defn; /* replacement text */
};

typedef struct {
	unsigned long length;
	struct nlist **table;
} hash_table_t;

static uint32_t hash(const char *s, size_t len) { /* DJB2 Hash, <http://www.cse.yorku.ca/~oz/hash.html> */
	assert(s);
	uint32_t h = 5381;
	for (size_t i = 0; i < len; s++, i++)
		h = ((h << 5) + h) + (*s);
	return h;
}

static char *dstrdup(const char *s) { /* make a duplicate of s */
	assert(s);
	char *p =  malloc(strlen(s) + 1); /* +1 for ’\0’ */
	if (p != NULL)
		strcpy(p, s);
	return p;
}

struct nlist *lookup(hash_table_t *h, const char *s) { /* lookup: look for s in hashtab */
	assert(s);
	assert(h);
	struct nlist *np;
	for (np = h->table[hash(s, strlen(s)) % h->length]; np != NULL; np = np->next)
		if (strcmp(s, np->name) == 0)
			return np; /* found */
	return NULL; /* not found */
}

struct nlist *install(hash_table_t *h, const char *name, const char *defn) { /* install: put (name, defn) in hashtab */
	assert(h);
	assert(name);
	assert(defn);
	struct nlist *np;
	if ((np = lookup(h, name)) == NULL) { /* not found */
		np = malloc(sizeof(*np));
		if (np == NULL || (np->name = dstrdup(name)) == NULL) {
			free(np);
			return NULL;
		}
		const unsigned long hashval = hash(name, strlen(name)) % h->length;
		np->next = h->table[hashval];
		h->table[hashval] = np;
	} else { /* already there */
		free(np->defn); /*free previous defn */
	}
	if ((np->defn = dstrdup(defn)) == NULL) /* error! */
		return NULL;
	return np;
}

#ifndef NDEBUG
#include <stdio.h>
typedef struct {
	char *name, *defn;
} test_t;

int hash_test(hash_table_t *h, const test_t *ts, size_t length) {
	assert(h);
	assert(ts);
	int r = 0;
	for (size_t i = 0; i < length; i++) {
		const test_t *t = &ts[i];
		assert(t->name);
		if (t->defn) {
			if (!install(h, t->name, t->defn)) {
				printf("Could not install %s\n", t->name);
				return -1;
			} else {
				printf("Installed: dict['%s'] = %s\n", t->name, t->defn);
			}
		}
	}

	for (size_t i = 0; i < length; i++) {
		const test_t *t = &ts[i];
		struct nlist *f = lookup(h, t->name);
		int pass = 1;
		const char *expected = t->defn, *actual = "(null)";
		if (f) {
			if (strcmp(t->defn, f->defn))
				pass = 0;
			actual = f->defn;
		} else {
			if (expected != NULL)
				pass = 0;
			expected = "(null)";
		}
		printf("%s dict['%s'] = '%s', expected '%s'\n",
				pass ? "   ok:" : " fail:",
				t->name, actual, expected);
		if (!pass)
			r = -1;
	}
	return r;
}

int main(void) {
	#define HASHSIZE (101)
	int r = 0;
	hash_table_t dictionary = { .length = HASHSIZE, .table = (struct nlist *[HASHSIZE]){ 0 } };
	hash_table_t *h = &dictionary;
	static const test_t ts[] = {
		{ "Form 27b/6", "Not filled in" },
		{ "What does the raven say?", "Nevermore" },
		{ "What does the fox say?", "No one knows" },
		{ "Alpha", "Bravo" },
		{ "Charlie", "Delta" },
		{ "404", NULL },
	};
	const size_t ts_length = sizeof(ts)/sizeof(ts[0]);

	if (hash_test(h, &ts[0], ts_length) < 0)
		r = -1;

	static const test_t replacements[] = {
		{ "Alpha",   "Omega" },
		{ "Charlie", "Cocaine!" },
	};

	const size_t replacements_length = sizeof(replacements)/sizeof(replacements[0]);
	if (hash_test(h, &replacements[0], replacements_length) < 0)
		r = -2;
	return r;
}

#endif

