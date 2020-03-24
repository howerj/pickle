#include "pickle.h"
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define LINESZ    (1024u)
#define PROGSZ    (1024u * 64u * 2u)
#define UNUSED(X) ((void)(X))

/* NB. Could store Heap status information in 'arena' and query with command */
static void *allocator(void *arena, void *ptr, const size_t oldsz, const size_t newsz) {
	UNUSED(arena);
	if (newsz == 0) {
		free(ptr);
		return NULL;
	}
	if (newsz > oldsz)
		return realloc(ptr, newsz);
	return ptr;
}

static int commandGets(pickle_t *i, int argc, char **argv, void *pd) {
	FILE *in = pd;
	if (argc != 1)
		return pickle_set_result_error_arity(i, 1, argc, argv);
	char buf[LINESZ] = { 0 };
	if (!fgets(buf, sizeof buf, in))
		return pickle_set_result_error(i, "EOF");
	return pickle_set_result_string(i, buf);
}

static int commandPuts(pickle_t *i, int argc, char **argv, void *pd) {
	FILE *out = pd;
	if (argc != 2 && argc != 3)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	int newline = 1;
	if (argc == 3) {
		if (!strcmp(argv[1], "-nonewline"))
			newline = 0;
		else
			return pickle_set_result(i, "Invalid option %s", argv[1]);
	}
	const int r = fputs(argc == 2 ? argv[1] : argv[2], out);
	if (newline)
		fputc('\n', out);
	return r < 0 ? PICKLE_ERROR : PICKLE_OK;
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
	UNUSED(pd);
	if (argc != 1 && argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	char *program = malloc(PROGSZ); /* !! */
	if (!program)
		return pickle_set_result_error(i, "Out Of Memory");
	errno = 0;
	FILE *file = argc == 1 ? stdin : fopen(argv[1], "rb");
	if (!file) {
		free(program);
		return pickle_set_result_error(i, "Could not open file '%s' for reading: %s", argv[1], strerror(errno));
	}
	program[fread(program, 1, PROGSZ - 1, file)] = '\0';
	if (file != stdin)
		fclose(file);
	const int r = pickle_eval(i, program);
	free(program);
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
		r = commandSource(i, 2, (char*[2]){ "source", argv[j] }, NULL);
		if (r != PICKLE_OK) {
			const char *f = NULL;
			if (pickle_get_result_string(i, &f) != PICKLE_OK)
				goto fail;
			(void)fprintf(stdout, "%s\n", f);
			goto fail;
		}
	}
	if (argc == 1)
		r = commandSource(i, 1, (char*[1]){ "source" }, NULL);
	return !!pickle_delete(i) || r != PICKLE_OK;
fail:
	pickle_delete(i);
	return 1;
}

