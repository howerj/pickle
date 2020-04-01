#include "pickle.h"
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define UNUSED(X) ((void)(X))

static void *allocator(void *arena, void *ptr, const size_t oldsz, const size_t newsz) {
	UNUSED(arena);
	if (newsz == 0) { free(ptr); return NULL; }
	if (newsz > oldsz) return realloc(ptr, newsz);
	return ptr;
}

static int commandGets(pickle_t *i, int argc, char **argv, void *pd) {
	FILE *in = pd;
	if (argc != 1)
		return pickle_set_result_error_arity(i, 1, argc, argv);
	char *m = NULL;
	for (size_t sz = 1, osz = 1;;) {
		char buf[128] = { 0 }; /* must be at least 2 bytes big */
		if (!fgets(buf, sizeof buf, in)) {
			free(m);
			if (pickle_set_result(i, "EOF") != PICKLE_OK)
				return PICKLE_ERROR;
			return PICKLE_BREAK;
		}
		osz = sz;
		sz += sizeof (buf) - 1ul;
		char *n = realloc(m, sz);
		if (!n) {
			free(m);
			return pickle_set_result_error(i, "Out Of Memory");
		}
		m = n;
		memcpy(&m[osz - 1], buf, sizeof buf);
		if (memchr(buf, '\n', sizeof buf))
			break;
	}
	const int r = pickle_set_result(i, m);
	free(m);
	return r;
}

static int commandPuts(pickle_t *i, int argc, char **argv, void *pd) {
	FILE *out = pd;
	if (argc != 1 && argc != 2 && argc != 3)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	if (argc == 1)
		return fputc('\n', out) < 0 ? PICKLE_ERROR : PICKLE_OK;
	if (argc == 2)
		return fprintf(out, "%s\n", argv[1]) < 0 ? PICKLE_ERROR : PICKLE_OK;
	if (!strcmp(argv[1], "-nonewline"))
		return fputs(argv[2], out) < 0 ? PICKLE_ERROR : PICKLE_OK;
	return pickle_set_result_error(i, "Invalid option %s", argv[1]);
}

static int commandGetEnv(pickle_t *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	const char *env = getenv(argv[1]);
	return pickle_set_result_string(i, env ? env : "");
}

static int commandExit(pickle_t *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2 && argc != 1)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	const char *code = argc == 2 ? argv[1] : "0";
	exit(atoi(code));
	return PICKLE_OK;
}

static char *slurp(FILE *input) {
	char *m = NULL;
	size_t sz = 0;
	for (;;) {
		char *n = realloc(m, sz + 4096 + 1);
		if (!n) {
			free(m);
			return NULL;
		}
		m = n;
		const size_t inc = fread(&m[sz], 1, 4096, input);
		sz += inc;
		if (inc != 4096)
			break;
	}
	m[sz] = '\0'; /* ensure NUL termination */
	return m;
}

static int commandSource(pickle_t *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 1 && argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	errno = 0;
	FILE *file = argc == 1 ? stdin : fopen(argv[1], "rb");
	if (!file)
		return pickle_set_result_error(i, "Could not open file '%s' for reading: %s", argv[1], strerror(errno));

	char *program = slurp(file);
	if (file != stdin)
		fclose(file);
	if (!program)
		return pickle_set_result_error(i, "Out Of Memory");

	const int r = pickle_eval(i, program);
	free(program);
	return r;
}

static int evalFile(pickle_t *i, char *file) {
	const int r = file ?
		commandSource(i, 2, (char*[2]){ "source", file }, NULL):
		commandSource(i, 1, (char*[1]){ "source",      }, NULL);
	if (r != PICKLE_OK) {
		const char *f = NULL;
		if (pickle_get_result_string(i, &f) != PICKLE_OK)
			return r;
		if (fprintf(stdout, "%s\n", f) < 0)
			return PICKLE_ERROR;
	}
	return r;
}

int main(int argc, char **argv) {
	pickle_t *i = NULL;
	if (pickle_tests(allocator, NULL)   != PICKLE_OK) goto fail;
	if (pickle_new(&i, allocator, NULL) != PICKLE_OK) goto fail;
	if (pickle_set_argv(i, argc, argv)  != PICKLE_OK) goto fail;
	if (pickle_register_command(i, "gets",   commandGets,   stdin)  != PICKLE_OK) goto fail;
	if (pickle_register_command(i, "puts",   commandPuts,   stdout) != PICKLE_OK) goto fail;
	if (pickle_register_command(i, "getenv", commandGetEnv, NULL)   != PICKLE_OK) goto fail;
	if (pickle_register_command(i, "exit",   commandExit,   NULL)   != PICKLE_OK) goto fail;
	if (pickle_register_command(i, "source", commandSource, NULL)   != PICKLE_OK) goto fail;
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
	pickle_delete(i);
	return 1;
}

