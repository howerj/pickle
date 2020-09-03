#include "pickle.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define UNUSED(X) ((void)(X))
#define ok(i, ...)    pickle_result_set(i, PICKLE_OK,    __VA_ARGS__)
#define error(i, ...) pickle_result_set(i, PICKLE_ERROR, __VA_ARGS__)

typedef struct { long allocs, frees, reallocs, total; } heap_t;

static void *allocator(void *arena, void *ptr, const size_t oldsz, const size_t newsz) {
	/* assert(h && (h->frees <= h->allocs)); */
	heap_t *h = arena;
	if (newsz == 0) { if (ptr) h->frees++; free(ptr); return NULL; }
	if (newsz > oldsz) { h->reallocs += !!ptr; h->allocs++; h->total += newsz; return realloc(ptr, newsz); }
	return ptr;
}

static int release(pickle_t *i, void *ptr) {
	void *arena = NULL;
	allocator_fn fn = NULL;
	const int r1 = pickle_allocator_get(i, &fn, &arena);
	if (fn)
		fn(arena, ptr, 0, 0);
	return fn ? r1 : PICKLE_ERROR;
}

static void *reallocator(pickle_t *i, void *ptr, size_t sz) {
	void *arena = NULL;
	allocator_fn fn = NULL;
	if (pickle_allocator_get(i, &fn, &arena) != PICKLE_OK)
		abort();
	void *r = allocator(arena, ptr, 0, sz);
	if (!r) {
		release(i, ptr);
		return NULL;
	}
	return r;
}

static char *slurp(pickle_t *i, FILE *input, size_t *length, char *class) {
	char *m = NULL;
	const size_t bsz = class ? 80 : 4096;
	size_t sz = 0;
	if (length)
		*length = 0;
	for (;;) {
		if ((m = reallocator(i, m, sz + bsz + 1)) == NULL)
			return NULL;
		if (class) {
			size_t j = 0;
			int ch = 0, done = 0;
			for (; ((ch = fgetc(input)) != EOF) && j < bsz; ) {
				m[sz + j++] = ch;
				if (strchr(class, ch)) {
					done = 1;
					break;
				}
			}
			sz += j;
			if (done || ch == EOF)
				break;
		} else {
			size_t inc = fread(&m[sz], 1, bsz, input);
			sz += inc;
			if (inc != bsz)
				break;
		}
	}
	m[sz] = '\0'; /* ensure NUL termination */
	if (length)
		*length = sz;
	return m;
}

static int commandGets(pickle_t *i, int argc, char **argv, void *pd) {
	if (argc != 1)
		return error(i, "Invalid command %s", argv[0]);
	size_t length = 0;
	char *line = slurp(i, (FILE*)pd, &length, "\n");
	if (!line)
		return error(i, "Out Of Memory");
	if (!length) {
		if (release(i, line) != PICKLE_OK)
			return PICKLE_ERROR;
		if (ok(i, "EOF") != PICKLE_OK)
			return PICKLE_ERROR;
		return PICKLE_BREAK;
	}
	const int r = ok(i, "%s", line);
	return release(i, line) == PICKLE_OK ? r : PICKLE_ERROR;
}

static int commandPuts(pickle_t *i, int argc, char **argv, void *pd) {
	FILE *out = pd;
	if (argc != 1 && argc != 2 && argc != 3)
		return error(i, "Invalid command %s", argv[0]);
	if (argc == 1)
		return fputc('\n', out) < 0 ? PICKLE_ERROR : PICKLE_OK;
	if (argc == 2)
		return fprintf(out, "%s\n", argv[1]) < 0 ? PICKLE_ERROR : PICKLE_OK;
	if (!strcmp(argv[1], "-nonewline"))
		return fputs(argv[2], out) < 0 ? PICKLE_ERROR : PICKLE_OK;
	return error(i, "Invalid option %s", argv[1]);
}

static int commandGetEnv(pickle_t *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2)
		return error(i, "Invalid command %s", argv[0]);
	const char *env = getenv(argv[1]);
	return ok(i, "%s", env ? env : "");
}

static int commandExit(pickle_t *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2 && argc != 1)
		return error(i, "Invalid command %s", argv[0]);
	const char *code = argc == 2 ? argv[1] : "0";
	exit(atoi(code));
	return PICKLE_ERROR; /* unreachable */
}

static int commandClock(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	time_t ts = 0;
	if (argc < 2)
		return error(i, "Invalid command %s", argv[0]);
	if (!strcmp(argv[1], "clicks")) {
		const long t = (((double)(clock()) / (double)CLOCKS_PER_SEC) * 1000.0);
		return ok(i, "%ld", t);
	}
	if (!strcmp(argv[1], "seconds"))
		return ok(i, "%ld", (long)time(&ts));
	if (!strcmp(argv[1], "format")) {
		const int gmt = 1;
		char buf[512] = { 0 };
		char *fmt = argc == 4 ? argv[3] : "%a %b %d %H:%M:%S %Z %Y";
		int tv = 0;
		if (argc != 3 && argc != 4)
			return error(i, "Invalid subcommand %s", argv[1]);
		if (sscanf(argv[2], "%d", &tv) != 1)
			return error(i, "Invalid number %s", argv[2]);
		ts = tv;
		struct tm *timeinfo = (gmt ? gmtime : localtime)(&ts);
		strftime(buf, sizeof buf, fmt, timeinfo);
		return ok(i, "%s", buf);
	}
	return error(i, "Invalid command %s", argv[0]);
}

static int commandSource(pickle_t *i, int argc, char **argv, void *pd) {
	if (argc != 1 && argc != 2)
		return error(i, "Invalid command %s", argv[0]);
	errno = 0;
	FILE *file = argc == 1 ? pd : fopen(argv[1], "rb");
	if (!file)
		return error(i, "Could not open file '%s' for reading: %s", argv[1], strerror(errno));
	char *program = slurp(i, file, NULL, NULL);
	if (file != pd)
		fclose(file);
	if (!program)
		return error(i, "Out Of Memory");
	const int r = pickle_eval(i, program);
	return release(i, program) == PICKLE_OK ? r : PICKLE_ERROR;
}

static int commandHeap(pickle_t *i, int argc, char **argv, void *pd) {
	heap_t *h = pd;
	if (argc != 2)
		return error(i, "Invalid command %s", argv[0]);
	if (!strcmp(argv[1], "frees"))         return ok(i, "%ld", h->frees);
	if (!strcmp(argv[1], "allocations"))   return ok(i, "%ld", h->allocs);
	if (!strcmp(argv[1], "total"))         return ok(i, "%ld", h->total);
	if (!strcmp(argv[1], "reallocations")) return ok(i, "%ld", h->reallocs);
	return error(i, "Invalid command %s", argv[0]);
}

static int evalFile(pickle_t *i, char *file) {
	const int r = file ?
		commandSource(i, 2, (char*[2]){ "source", file }, NULL):
		commandSource(i, 1, (char*[1]){ "source",      }, stdin);
	if (r != PICKLE_OK) {
		const char *f = NULL;
		if (pickle_result_get(i, &f) != PICKLE_OK)
			return r;
		if (fprintf(stdout, "%s\n", f) < 0)
			return PICKLE_ERROR;
	}
	return r;
}

static int setArgv(pickle_t *i, int argc, char **argv) {
	const char *r = NULL;
	char **l = reallocator(i, NULL, (argc + 1) * sizeof (char*));
	if (!l)
		return PICKLE_ERROR;
	memcpy(&l[1], argv, argc * sizeof (char*));
	l[0] = "list";
	if (pickle_eval_args(i, argc + 1, l) != PICKLE_OK)
		goto fail;
	if (pickle_result_get(i, &r) != PICKLE_OK)
		goto fail;
	if (pickle_eval_args(i, 3, (char*[3]){"set", "argv", (char*)r}) != PICKLE_OK)
		goto fail;
	return release(i, l);
fail:
	(void)release(i, l);
	return PICKLE_ERROR;
}

int main(int argc, char **argv) {
	heap_t h = { 0, 0, 0, 0 };
	pickle_t *i = NULL;
	if (pickle_tests(allocator, &h)   != PICKLE_OK) goto fail;
	if (pickle_new(&i, allocator, &h) != PICKLE_OK) goto fail;
	if (setArgv(i, argc, argv)  != PICKLE_OK) goto fail;
	if (pickle_command_register(i, "gets",   commandGets,   stdin)  != PICKLE_OK) goto fail;
	if (pickle_command_register(i, "puts",   commandPuts,   stdout) != PICKLE_OK) goto fail;
	if (pickle_command_register(i, "getenv", commandGetEnv, NULL)   != PICKLE_OK) goto fail;
	if (pickle_command_register(i, "exit",   commandExit,   NULL)   != PICKLE_OK) goto fail;
	if (pickle_command_register(i, "source", commandSource, NULL)   != PICKLE_OK) goto fail;
	if (pickle_command_register(i, "clock",  commandClock,  NULL)   != PICKLE_OK) goto fail;
	if (pickle_command_register(i, "heap",   commandHeap,   &h)     != PICKLE_OK) goto fail;
	int r = 0;
	for (int j = 1; j < argc; j++) {
		r = evalFile(i, argv[j]);
		if (r < 0)
			goto fail;
		if (r == PICKLE_BREAK)
			break;
	}
	if (argc == 1)
		r = evalFile(i, NULL);
	return !!pickle_delete(i) || r < 0;
fail:
	(void)pickle_delete(i);
	return 1;
}

