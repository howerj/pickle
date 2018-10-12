/* https://stackoverflow.com/questions/4384359/
 * The C Programming language 
 *
 * @todo integrate this small dictionary library into 'pickle.c',
 * replacing the (slow) command lookup */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct nlist { /* table entry: */
	struct nlist *next; /* next entry in chain */
	char *name; /* defined name */
	char *defn; /* replacement text */
};

#define HASHSIZE 101
static struct nlist *hashtab[HASHSIZE]; /* pointer table */

/*uint32_t djb2(const char *s, size_t len) { // DJB2 Hash
	assert(s);
	uint32_t h = 5381;
	for (size_t i = 0; i < len; s++, i++)
		h = ((h << 5) + h) + (*s);
	return h;
}*/

unsigned hash(char *s) { /* hash: form hash value for string s */
	assert(s);
	unsigned hashval;
	for (hashval = 0; *s != '\0'; s++)
		hashval = *s + 31 * hashval;
	return hashval % HASHSIZE;
}

char *strdup(const char *s) { /* make a duplicate of s */
	assert(s);
	char *p =  malloc(strlen(s)+1); /* +1 for ’\0’ */
	if (p != NULL)
		strcpy(p, s);
	return p;
}

struct nlist *lookup(char *s) { /* lookup: look for s in hashtab */
	assert(s);
	struct nlist *np;
	for (np = hashtab[hash(s)]; np != NULL; np = np->next)
		if (strcmp(s, np->name) == 0)
			return np; /* found */
	return NULL; /* not found */
}

struct nlist *install(char *name, char *defn) { /* install: put (name, defn) in hashtab */
	assert(name);
	assert(defn);
	struct nlist *np;
	if ((np = lookup(name)) == NULL) { /* not found */
		np = malloc(sizeof(*np));
		if (np == NULL || (np->name = strdup(name)) == NULL) {
			free(np);
			return NULL;
		}
		unsigned hashval = hash(name);
		np->next = hashtab[hashval];
		hashtab[hashval] = np;
	} else /* already there */
		free((void *) np->defn); /*free previous defn */
	if ((np->defn = strdup(defn)) == NULL)
		return NULL;
	return np;
}

