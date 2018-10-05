#include "pickle.h"
#include "block.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#define FILE_SZ (1024 * 16)

static int picolCommandPuts(pickle_t *i, int argc, char **argv, void *pd) {
	assert(i);
	assert(argv);
	assert(pd);
	if (argc != 2)
		return pickle_arity_error(i, 2, argv[0]);
	fprintf((FILE*)pd, "%s\n", argv[1]);
	return PICKLE_OK;
}

static int picolCommandGets(pickle_t *i, int argc, char **argv, void *pd) {
	assert(i);
	assert(argv);
	assert(pd);
	if (argc != 1)
		return pickle_arity_error(i, 1, argv[0]);
	char buf[PICKLE_MAX_STRING] = { 0 };
	(void)/*ignore result*/fgets(buf, sizeof buf, (FILE*)pd);
	pickle_set_result(i, buf);
	return PICKLE_OK;
}

static int picolCommandSystem(pickle_t *i, int argc, char **argv, void *pd) {
	assert(i);
	assert(argv);
	UNUSED(pd);
	if (argc != 2)
		return pickle_arity_error(i, 2, argv[0]);
	char v[64];
	const int r = system(argv[1]);
	snprintf(v, sizeof v, "%d", r);
	pickle_set_result(i, v);
	return PICKLE_OK;
}

static int picolCommandRand(pickle_t *i, int argc, char **argv, void *pd) {
	assert(i);
	assert(argv);
	UNUSED(pd);
	if (argc != 1)
		return pickle_arity_error(i, 1, argv[0]);
	char v[64];
	snprintf(v, sizeof v, "%d", rand());
	pickle_set_result(i, v);
	return PICKLE_OK;
}

static int picolCommandExit(pickle_t *i, int argc, char **argv, void *pd) {
	assert(i);
	assert(argv);
	UNUSED(pd);
	if (argc != 2 && argc != 1)
		return pickle_arity_error(i, 2, argv[0]);
	const char *code = argc == 2 ? argv[1] : "0";
	exit(atoi(code));
	return PICKLE_OK;
}

static int picolCommandGetEnv(pickle_t *i, int argc, char **argv, void *pd) {
	assert(i);
	assert(argv);
	UNUSED(pd);
	if (argc != 2)
		return pickle_arity_error(i, 2, argv[0]);
	char *env = getenv(argv[1]);
	pickle_set_result(i, env ? env : "");
	return PICKLE_OK;
}

static int picolCommandStrftime(pickle_t *i, int argc, char **argv, void *pd) {
	assert(i);
	assert(argv);
	UNUSED(pd);
	if (argc != 2)
		return pickle_arity_error(i, 2, argv[0]);
	char buf[PICKLE_MAX_STRING] = { 0 };
	time_t rawtime;
	time(&rawtime);
	struct tm *timeinfo = gmtime(&rawtime);
	strftime(buf, sizeof buf, argv[1], timeinfo);
	pickle_set_result(i, buf);
	return PICKLE_OK;
}

/*From <http://c-faq.com/lib/regex.html>*/
static int match(const char *pat, const char *str) {
	assert(pat);
	assert(str);
	switch(*pat) {
	case '\0':  return !*str;
	case '*':   return match(pat+1, str) || (*str && match(pat, str+1));
	case '.':   return *str && match(pat+1, str+1);
	default:    return *pat == *str && match(pat+1, str+1);
	}
}

static int picolCommandMatch(pickle_t *i, int argc, char **argv, void *pd) {
	assert(i);
	assert(argv);
	UNUSED(pd);
	if (argc != 3)
		return pickle_arity_error(i, 3, argv[0]);
	pickle_set_result(i, match(argv[1], argv[2]) ? argv[2] : "");
	return PICKLE_OK;
}

static int picolCommandEqual(pickle_t *i, int argc, char **argv, void *pd) {
	assert(i);
	assert(argv);
	UNUSED(pd);
	if (argc != 3)
		return pickle_arity_error(i, 3, argv[0]);
	pickle_set_result(i, !strcmp(argv[1], argv[2]) ? "1" : "0");
	return PICKLE_OK;
}

void *custom_malloc(void *a, size_t length)           { return pool_malloc(a, length); }
void custom_free(void *a, void *v)                    { pool_free(a, v); }
void *custom_realloc(void *a, void *v, size_t length) { return pool_realloc(a, v, length); }

int main(int argc, char **argv) {
	int r = 0;
	FILE *input = stdin, *output = stdout;
	int use_custom_allocator = 1;

	allocator_t allocator = {
		.free    = custom_free,
		.realloc = custom_realloc,
		.malloc  = custom_malloc,
		.arena   = use_custom_allocator ? pool_new() : NULL,
	};

	/*if(use_custom_allocator)
		block_test();*/

	pickle_t interp = { .initialized = 0 };
	pickle_initialize(&interp, use_custom_allocator ? &allocator : NULL);

	const pickle_register_command_t commands[] = {
		{ "puts",     picolCommandPuts,     stdout },
	//	{ "eputs",    picolCommandPuts,     stderr },
		{ "gets",     picolCommandGets,     stdin },
		{ "system",   picolCommandSystem,   NULL },
		{ "exit",     picolCommandExit,     NULL },
		{ "quit",     picolCommandExit,     NULL },
		{ "getenv",   picolCommandGetEnv,   NULL },
		{ "rand",     picolCommandRand,     NULL },
		{ "strftime", picolCommandStrftime, NULL },
		{ "match",    picolCommandMatch,    NULL },
		{ "eq",       picolCommandEqual,    NULL },
	};
	for(size_t j = 0; j < sizeof(commands)/sizeof(commands[0]); j++)
		if(pickle_register_command(&interp, commands[j].name, commands[j].func, commands[j].data) != 0) {
			r = -1;
			goto end;
		}

	if (argc == 1) {
		for(;;) {
			char clibuf[PICKLE_MAX_STRING];
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
	FILE *fp = fopen(argv[1], "rb");
	if (!fp) {
		fprintf(stderr, "failed to open file %s: %s\n", argv[1], strerror(errno));
		r = -1;
		goto end;
	}
	char buf[PICKLE_MAX_STRING*16];
	buf[fread(buf, 1, PICKLE_MAX_STRING*16, fp)] = '\0';
	fclose(fp);
	if (pickle_eval(&interp, buf) != 0)
		fprintf(output, "%s\n", interp.result);
end:
	pickle_deinitialize(&interp);
	if(use_custom_allocator)
		pool_delete(allocator.arena);
	return r;
}

