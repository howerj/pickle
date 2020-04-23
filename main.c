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

static char *slurp(pickle_t *i, FILE *input, size_t *length, char *class) {
	char *m = NULL;
	const size_t bsz = class ? 4096 : 80;
	size_t sz = 0;
	if (length)
		*length = 0;
	for (;;) {
		if (pickle_reallocate(i, (void**)&m, sz + bsz + 1) != PICKLE_OK)
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
		return pickle_set_result_error_arity(i, 1, argc, argv);
	size_t length = 0;
	char *line = slurp(i, (FILE*)pd, &length, "\n");
	if (!line)
		return pickle_set_result_error(i, "Out Of Memory");
	if (!length) {
		if (pickle_free(i, (void**)&line) != PICKLE_OK)
			return PICKLE_ERROR;
		if (pickle_set_result(i, "EOF") != PICKLE_OK)
			return PICKLE_ERROR;
		return PICKLE_BREAK;
	}
	const int r = pickle_set_result(i, line);
	if (pickle_free(i, (void**)&line) != PICKLE_OK)
		return PICKLE_ERROR;
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

static int commandSource(pickle_t *i, int argc, char **argv, void *pd) {
	if (argc != 1 && argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	errno = 0;
	FILE *file = argc == 1 ? pd : fopen(argv[1], "rb");
	if (!file)
		return pickle_set_result_error(i, "Could not open file '%s' for reading: %s", argv[1], strerror(errno));

	char *program = slurp(i, file, NULL, NULL);
	if (file != pd)
		fclose(file);
	if (!program)
		return pickle_set_result_error(i, "Out Of Memory");

	const int r = pickle_eval(i, program);
	if (pickle_free(i, (void**)&program) != PICKLE_OK)
		return PICKLE_ERROR;
	return r;
}

static int evalFile(pickle_t *i, char *file) {
	const int r = file ?
		commandSource(i, 2, (char*[2]){ "source", file }, NULL):
		commandSource(i, 1, (char*[1]){ "source",      }, stdin);
	if (r != PICKLE_OK) {
		const char *f = NULL;
		if (pickle_get_result_string(i, &f) != PICKLE_OK)
			return r;
		if (fprintf(stdout, "%s\n", f) < 0)
			return PICKLE_ERROR;
	}
	return r;
}

static int setArgv(pickle_t *i, int argc, char **argv) {
	char *args = NULL;
	int r = PICKLE_ERROR;
	if ((pickle_concatenate(i, argc, argv, &args) != PICKLE_OK) || args == NULL)
		goto done;
	r = pickle_set_var_string(i, "argv", args);
done:
	if (pickle_free(i, (void**)&args) != PICKLE_OK)
		return PICKLE_ERROR;
	return r;
}

int main(int argc, char **argv) {
	pickle_t *i = NULL;
	if (pickle_tests(allocator, NULL)   != PICKLE_OK) goto fail;
	if (pickle_new(&i, allocator, NULL) != PICKLE_OK) goto fail;
	if (setArgv(i, argc, argv)  != PICKLE_OK) goto fail;
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

