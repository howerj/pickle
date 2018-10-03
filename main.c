#include "pickle.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define FILE_SZ (1024 * 16)

static int picolCommandPuts(pickle_t *i, int argc, char **argv, void *pd) {
	assert(i);
	assert(argv);
	assert(pd);
	if (argc != 2) 
		return pickle_arity_error(i, argv[0]);
	fprintf((FILE*)pd, "%s\n", argv[1]);
	return 0;
}

static int picolCommandGets(pickle_t *i, int argc, char **argv, void *pd) {
	assert(i);
	assert(argv);
	assert(pd);
	if (argc != 1) 
		return pickle_arity_error(i, argv[0]);
	char buf[1024];
	fgets(buf, sizeof buf, (FILE*)pd);
	pickle_set_result(i, buf);
	return 0;
}

static int picolCommandSystem(pickle_t *i, int argc, char **argv, void *pd) {
	assert(i);
	assert(argv);
	(void)pd;
	char v[64];
	if (argc != 2)
		return pickle_arity_error(i, argv[0]);
	const int r = system(argv[1]);
	snprintf(v, sizeof v, "%d", r);
	pickle_set_result(i, v);
	return 0;
}

static int picolCommandExit(pickle_t *i, int argc, char **argv, void *pd) {
	assert(i);
	assert(argv);
	(void)pd;
	if (argc != 2)
		return pickle_arity_error(i, argv[0]);
	exit(atoi(argv[1]));
	return 0;
}

static int picolCommandGetEnv(pickle_t *i, int argc, char **argv, void *pd) {
	assert(i);
	assert(argv);
	(void)pd;
	if (argc != 2)
		return pickle_arity_error(i, argv[0]);
	char *env = getenv(argv[1]);
	pickle_set_result(i, env ? env : "");
	return 0;
}

int main(int argc, char **argv) {
	int r = 0;
	FILE *input = stdin, *output = stdout;
	pickle_t interp = { .initialized = 0 };
	pickle_initialize(&interp);

	pickle_register_command(&interp, "puts",   picolCommandPuts,   stdout);
	pickle_register_command(&interp, "gets",   picolCommandGets,   stdin);
	pickle_register_command(&interp, "system", picolCommandSystem, NULL);
	pickle_register_command(&interp, "exit",   picolCommandExit,   NULL);
	pickle_register_command(&interp, "getenv", picolCommandGetEnv, NULL);

	if (argc == 1) {
		for(;;) {
			char clibuf[1024];
			fprintf(output, "pickle> "); 
			fflush(output);
			if (!fgets(clibuf, sizeof clibuf, input))
				goto end;
			const int retcode = pickle_eval(&interp,clibuf);
			if (interp.result[0] != '\0')
				fprintf(output, "[%d] %s\n", retcode, interp.result);
		}
	}
 	if(argc != 2) {
		fprintf(stderr, "usage: %s file\n", argv[0]);
		r = -1;
		goto end;
	}	
	FILE *fp = fopen(argv[1], "r");
	if (!fp) {
		fprintf(stderr, "failed to open file %s: %s\n", argv[1], strerror(errno));
		r = -1;
		goto end;
	}
	char buf[1024*16];
	buf[fread(buf, 1, 1024*16, fp)] = '\0';
	fclose(fp);
	if (pickle_eval(&interp, buf) != 0) 
		fprintf(output, "%s\n", interp.result);
end:
	pickle_deinitialize(&interp);
	return r;
}

