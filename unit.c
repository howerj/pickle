/* TODO: Replace 'main.c' with this file after adding arena allocator */
#include "pickle.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define LINESZ    (512u)
#define PROGSZ    (1024u * 64u)
#define UNUSED(X) ((void)(X))

static int CommandGets(pickle_t *i, int argc, char **argv, void *pd) {
	FILE *in = pd;
	if (argc != 1)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	char buf[LINESZ] = { 0 };
	if (!fgets(buf, sizeof buf, in))
		return pickle_set_result_error(i, "EOF");
	return pickle_set_result_string(i, buf);
}

static int CommandPuts(pickle_t *i, int argc, char **argv, void *pd) {
	FILE *out= pd;
	if (argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	return fprintf(out, "%s\n", argv[1]) < 0 ? PICKLE_ERROR : PICKLE_OK;
}

static int CommandGetEnv(pickle_t *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	const char *env = getenv(argv[1]);
	return pickle_set_result_string(i, env ? env : "");
}

static int CommandExit(pickle_t *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2 && argc != 1)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	const char *code = argc == 2 ? argv[1] : "0";
	exit(atoi(code));
	return PICKLE_OK;
}

int main(int argc, char **argv) {
	pickle_t *i = NULL;
	if (pickle_tests() != PICKLE_OK) goto fail;
	if (pickle_new(&i, NULL) != PICKLE_OK) goto fail;
	if (pickle_set_argv(i, argc, argv) != PICKLE_OK) goto fail;
	if (pickle_register_command(i, "gets",   CommandGets,   stdin)  != PICKLE_OK) goto fail;
	if (pickle_register_command(i, "puts",   CommandPuts,   stdout) != PICKLE_OK) goto fail;
	if (pickle_register_command(i, "getenv", CommandGetEnv, NULL)   != PICKLE_OK) goto fail;
	if (pickle_register_command(i, "exit",   CommandExit,   NULL)   != PICKLE_OK) goto fail;

	if (argc > 1) {
		static char program[PROGSZ] = { 0 }; /* !! */
		for (int j = 1; j < argc; j++) {
			FILE *f = fopen(argv[j], "rb");
			if (!f) {
				fprintf(stderr, "Invalid file %s\n", argv[j]);
				goto fail;
			}
			program[fread(program, 1, PROGSZ, f)] = '\0';
			const int err = pickle_eval(i, program);
			fclose(f);
			if (err != PICKLE_OK) {
				const char *r = NULL;
				pickle_get_result_string(i, &r);
				fprintf(stdout, "%s\n", r);
				goto fail;
			}
		}
	} else {
		const char *prompt = "> ";
		fputs(prompt, stdout);
		fflush(stdout);
		for (char buf[512] = { 0 }; fgets(buf, sizeof buf, stdin); memset(buf, 0, sizeof buf)) {
			const char *r = NULL;
			const int err = pickle_eval(i, buf);
			pickle_get_result_string(i, &r);
			fprintf(stdout, "[%d]: %s\n%s", err, r, prompt);
			fflush(stdout);
		}
	}
	return !!pickle_delete(i);
fail:
	pickle_delete(i);
	return 1;
}
