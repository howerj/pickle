#include "pickle.h"
#include "block.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <time.h>

#define LINE_SZ (1024)
#define FILE_SZ (1024 * 16)

static int picolCommandPuts(pickle_t *i, int argc, char **argv, void *pd) {
	assert(pd);
	if (argc != 2)
		return pickle_arity_error(i, 2, argv[0]);
	fprintf((FILE*)pd, "%s\n", argv[1]);
	return PICKLE_OK;
}

static int picolCommandGets(pickle_t *i, int argc, char **argv, void *pd) {
	assert(pd);
	if (argc != 1)
		return pickle_arity_error(i, 1, argv[0]);
	char buf[PICKLE_MAX_STRING] = { 0 };
	(void)/*ignore result*/fgets(buf, sizeof buf, (FILE*)pd);
	pickle_set_result(i, buf);
	return PICKLE_OK;
}

static int picolCommandSystem(pickle_t *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2 && argc != 1)
		return pickle_arity_error(i, 2, argv[0]);
	char v[64];
	const int r = system(argc == 1 ? NULL : argv[1]);
	snprintf(v, sizeof v, "%d", r);
	pickle_set_result(i, v);
	return PICKLE_OK;
}

static int picolCommandRandom(pickle_t *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 1)
		return pickle_arity_error(i, 1, argv[0]);
	char v[64];
	snprintf(v, sizeof v, "%d", rand());
	pickle_set_result(i, v);
	return PICKLE_OK;
}

static int picolCommandExit(pickle_t *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2 && argc != 1)
		return pickle_arity_error(i, 2, argv[0]);
	const char *code = argc == 2 ? argv[1] : "0";
	exit(atoi(code));
	return PICKLE_OK;
}

static int picolCommandGetEnv(pickle_t *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2)
		return pickle_arity_error(i, 2, argv[0]);
	char *env = getenv(argv[1]);
	pickle_set_result(i, env ? env : "");
	return PICKLE_OK;
}

static int picolCommandStrftime(pickle_t *i, int argc, char **argv, void *pd) {
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
	case '*':   return match(pat + 1, str) || (*str && match(pat, str + 1));
	case '?':   return *str && match(pat + 1, str + 1);
	default:    return *pat == *str && match(pat + 1, str + 1);
	}
}

static int picolCommandMatch(pickle_t *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 3)
		return pickle_arity_error(i, 3, argv[0]);
	pickle_set_result(i, match(argv[1], argv[2]) ? argv[2] : "");
	return PICKLE_OK;
}

static int picolCommandEqual(pickle_t *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 3)
		return pickle_arity_error(i, 3, argv[0]);
	pickle_set_result(i, !strcmp(argv[1], argv[2]) ? "1" : "0");
	return PICKLE_OK;
}

static int picolCommandLength(pickle_t *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2)
		return pickle_arity_error(i, 2, argv[0]);
	char v[64];
	snprintf(v, sizeof v, "%u", (unsigned)strlen(argv[1])/*strnlen(argv[2], PICKLE_MAX_STRING)*/);
	pickle_set_result(i, v);
	return PICKLE_OK;
}

static int picolCommandRaise(pickle_t *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2)
		return pickle_arity_error(i, 2, argv[0]);
	char v[64];
	snprintf(v, sizeof v, "%d", raise(atoi(argv[1])));
	pickle_set_result(i, v);
	return PICKLE_OK;
}

static int global_argc = 0;
static char **global_argv = NULL;

static int picolCommandArgv(pickle_t *i, int argc, char **argv, void *pd) {
	UNUSED(pd);
	assert(global_argv);
	if(argc != 1 && argc != 2)
		return pickle_arity_error(i, 2, argv[0]);
	if(argc == 1) {
		char v[64];
		snprintf(v, sizeof v, "%d", global_argc);
		pickle_set_result(i, v);
		return PICKLE_OK;
	}
	int j = atoi(argv[1]);
	j = j > (global_argc - 1) ? global_argc - 1 : j;
	j = j < 0 ? 0 : j;
	pickle_set_result(i, global_argv[j]);
	return PICKLE_OK;
}

static int register_custom_commands(pickle_t *i) {
	assert(i);
	const pickle_register_command_t commands[] = {
		{ "puts",     picolCommandPuts,     stdout },
		{ "gets",     picolCommandGets,     stdin },
		{ "system",   picolCommandSystem,   NULL },
		{ "exit",     picolCommandExit,     NULL },
		{ "quit",     picolCommandExit,     NULL },
		{ "getenv",   picolCommandGetEnv,   NULL },
		{ "random",   picolCommandRandom,   NULL },
		{ "strftime", picolCommandStrftime, NULL },
		{ "match",    picolCommandMatch,    NULL },
		{ "eq",       picolCommandEqual,    NULL },
		{ "length",   picolCommandLength,   NULL },
		{ "raise",    picolCommandRaise,    NULL },
		{ "argv",     picolCommandArgv,     NULL },
	};
	for(size_t j = 0; j < sizeof(commands)/sizeof(commands[0]); j++)
		if(pickle_register_command(i, commands[j].name, commands[j].func, commands[j].data) != 0)
			return -1;
	return 0;
}

static int interactive(pickle_t *i, FILE *input, FILE *output, int prompt) {
	assert(i);
	assert(input);
	assert(output);
	for(;;) {
		char clibuf[LINE_SZ];
		if (prompt) {
			fprintf(output, "pickle> ");
			fflush(output);
		}
		if (!fgets(clibuf, sizeof clibuf, input))
			return 0;
		const int retcode = pickle_eval(i, clibuf);
		if (i->result[0] != '\0')
			fprintf(output, "[%d] %s\n", retcode, i->result);
	}
	return 0;
}

static int file(pickle_t *i, char *name, FILE *output) {
	assert(i);
	assert(file);
	assert(output);
	errno = 0;
	FILE *fp = fopen(name, "rb");
	if (!fp) {
		fprintf(stderr, "failed to open file %s (rb): %s\n", name, strerror(errno));
		return -1;
	}
	char buf[FILE_SZ];
	buf[fread(buf, 1, FILE_SZ, fp)] = '\0';
	fclose(fp);
	int retcode = PICKLE_OK;
	if ((retcode = pickle_eval(i, buf)) != PICKLE_OK)
		fprintf(output, "%s\n", i->result);
	return retcode == PICKLE_OK ? 0 : -1;
}

static int tests(void) {
#ifndef NDEBUG
	if (block_tests() < 0)
		return -1;
	if (pickle_tests() < 0)
		return -1;
#endif
	return 0;
}

static void help(char *arg0) {
	assert(arg0);
	static const char *msg = "\
Usage: %s file.tcl...\n\n\
pickle:     A tiny TCL like language derived/copied from 'picol'\n\
license:    BSD\n\
repository: https://github.com/howerj/pickle\n\
\n\
Options:\n\
\n\
--\tstop processing command line arguments\n\
-h\tdisplay this help message and exit\n\
-t\trun built in self tests and exit (return code 0 is success)\n\
-a\tuse custom block allocator, for testing purposes\n\
-s\tsuppress prompt printing\n\
\n\
If no arguments are given then input is taken from stdin. Otherwise\n\
they are treated as scripts to execute. Maximum file size is %d\n\
bytes, maximum length of an input line is is %d bytes.\n\
\n\
";
	fprintf(stdout, msg, arg0, FILE_SZ, LINE_SZ);
}

void *custom_malloc(void *a, size_t length)           { return pool_malloc(a, length); }
void custom_free(void *a, void *v)                    { pool_free(a, v); }
void *custom_realloc(void *a, void *v, size_t length) { return pool_realloc(a, v, length); }

int main(int argc, char **argv) {
	int r = 0, use_custom_allocator = 0, prompt_on = 1, j;

	global_argc = argc;
	global_argv = argv;

	static const pool_specification_t specs[] = {
		{ 8, 1024 }, /* most allocations are quite small */
		{ 64, 128 },
		{ 512, 16 }, /* maximum string length is bounded by this */
	};

	for(j = 1; j < argc; j++) {
		if(!strcmp(argv[j], "--")) {
			j++;
			break;
		} else if(!strcmp(argv[j], "-a")) {
			use_custom_allocator = 1;
		} else if(!strcmp(argv[j], "-s")) {
			prompt_on = 0;
		} else if(!strcmp(argv[j], "-h")) {
			help(argv[0]);
			return 0;
		} else if(!strcmp(argv[j], "-t")) {
			return tests();
		} else {
			break;
		}
	}
	argc -= j;
	argv += j;

	pickle_allocator_t allocator = {
		.free    = custom_free,
		.realloc = custom_realloc,
		.malloc  = custom_malloc,
		.arena   = use_custom_allocator ? pool_new(sizeof(specs)/sizeof(specs[0]), &specs[0]) : NULL,
	};

	pickle_t interp = { .initialized = 0 };
	if((r = pickle_initialize(&interp, use_custom_allocator ? &allocator : NULL)) < 0)
		goto end;
	if((r = register_custom_commands(&interp)) < 0)
		goto end;

	if (argc == 0) {
		r = interactive(&interp, stdin, stdout, prompt_on);
	} else {
		for(j = 0; j < argc; j++)
			if((r = file(&interp, argv[j], stdout)) != PICKLE_OK)
				break;
	}

end:
	pickle_deinitialize(&interp);
	if(use_custom_allocator)
		pool_delete(allocator.arena);
	return r;
}

