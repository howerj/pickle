/**@file main.c
 * @brief Extensions and driver for the 'pickle' interpreter. The
 * interpreter is a copy and modification of the 'picol' interpreter
 * by antirez. See the 'pickle.h' header for more information.
 * @author Richard James Howe
 * @license BSD */
#include "pickle.h"
#include "block.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <limits.h>
#include <time.h>

#define LINE_SZ (1024)
#define FILE_SZ (1024 * 16)
#define UNUSED(X) ((void)(X))

typedef struct {
	const char *name;
	pickle_command_func_t func;
	void *data;
} pickle_register_command_t;

typedef struct { int argc; char **argv; } argument_t;

static int use_custom_allocator = 0;
static pickle_t *interp;

void *custom_malloc(void *a, size_t length)           { return pool_malloc(a, length); }
void custom_free(void *a, void *v)                    { pool_free(a, v); }
void *custom_realloc(void *a, void *v, size_t length) { return pool_realloc(a, v, length); }

static pickle_allocator_t block_allocator = {
	.free    = custom_free,
	.realloc = custom_realloc,
	.malloc  = custom_malloc,
	.arena   = NULL
};

static int pickleCommandPuts(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(pd);
	if (argc != 2 && argc != 3)
		return pickle_error_arity(i, 3, argc, argv);
	int newline = 1;
	char *line = argv[1];
	if (argc == 3) {
		line = argv[2];
		if (!strcmp(argv[1], "-nonewline")) { newline = 0; }
		else return pickle_error(i, "Unknown puts command %s", argv[1]);
	}
	int r1 = 0;
	if (newline)
		r1 = fprintf((FILE*)pd, "%s\n", line);
	else
		r1 = fputs(line, (FILE*)pd);
	const int r2 = fflush((FILE*)pd);
	if (r1 < 0 || r2 < 0)
		return pickle_error(i, "I/O error: %d/%d", r1, r2);
	return PICKLE_OK;
}

static int pickleCommandGets(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(pd);
	if (argc != 1)
		return pickle_error_arity(i, 1, argc, argv);
	char buf[PICKLE_MAX_STRING] = { 0 };
	if (!fgets(buf, sizeof buf, (FILE*)pd)) {
		if (pickle_set_result_string(i, "EOF") != PICKLE_OK)
			return pickle_error_out_of_memory(i);
		return PICKLE_ERROR;
	}
	return pickle_set_result_string(i, buf);
}

static int pickleCommandError(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(pd);
	if (argc != 2)
		return pickle_error_arity(i, 2, argc, argv);
	const int r1 = fprintf((FILE*)pd, "%s\n", argv[1]);
	const int r2 = fflush((FILE*)pd);
	if (r1 < 0 || r2 < 0)
		return pickle_error(i, "I/O error: %d/%d", r1, r2);
	return PICKLE_ERROR;
}

static int pickleCommandSystem(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2 && argc != 1)
		return pickle_error_arity(i, 2, argc, argv);
	const int r = system(argc == 1 ? NULL : argv[1]);
	return pickle_set_result_integer(i, r);
}

static int pickleCommandRandom(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 1 && argc != 2)
		return pickle_error_arity(i, 2, argc, argv);
	if (argc == 2) {
		srand(atol(argv[1]));
		return PICKLE_OK;
	}
	return pickle_set_result_integer(i, rand());
}

static int pickleCommandExit(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2 && argc != 1)
		return pickle_error_arity(i, 2, argc, argv);
	const char *code = argc == 2 ? argv[1] : "0";
	exit(atoi(code));
	return PICKLE_OK;
}

static int pickleCommandGetEnv(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2)
		return pickle_error_arity(i, 2, argc, argv);
	const char *env = getenv(argv[1]);
	return pickle_set_result_string(i, env ? env : "");
}

static int pickleCommandClock(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc == 1) {
		const long t = (((double)(clock()) / (double)CLOCKS_PER_SEC) * 1000.0);
		return pickle_set_result_integer(i, t);
	}
	if (argc != 2)
		return pickle_error_arity(i, 2, argc, argv);
	char buf[PICKLE_MAX_STRING] = { 0 };
	time_t rawtime;
	time(&rawtime);
	struct tm *timeinfo = gmtime(&rawtime);
	strftime(buf, sizeof buf, argv[1], timeinfo);
	return pickle_set_result_string(i, buf);
}

/*Based on: <http://c-faq.com/lib/regex.html>*/
static int match(const char *pat, const char *str, size_t depth) {
	if (!depth) return -1;
 again:
        switch (*pat) {
	case '\0': return !*str;
	case '*': { /* match any number of characters: normally '.*' */
		const int r = match(pat + 1, str, depth - 1);
		if (r) return r;
		if (!*(str++)) return 0;
		goto again;
	}
	case '?':  /* match any single characters: normally '.' */
		if (!*str) return 0;
		pat++, str++;
		goto again;
	case '%': /* escape character: normally backslash */
		if (!*(++pat)) return -2; /* missing escaped character */
		if (!*str)     return 0;
		/* fall through */
	default:
		if (*pat != *str) return 0;
		pat++, str++;
		goto again;
	}
	return -3; /* not reached */
}

static int pickleCommandMatch(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 3)
		return pickle_error_arity(i, 3, argc, argv);
	const int r = match(argv[1], argv[2], PICKLE_MAX_RECURSION);
	if (r < 0)
		return pickle_error(i, "Regex error: %d", r);
	return pickle_set_result_string(i, r ? "1" : "0");
}

static int pickleCommandEqual(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 3)
		return pickle_error_arity(i, 3, argc, argv);
	return pickle_set_result_string(i, !strcmp(argv[1], argv[2]) ? "1" : "0");
}

static int pickleCommandNotEqual(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 3)
		return pickle_error_arity(i, 3, argc, argv);
	return pickle_set_result_string(i, strcmp(argv[1], argv[2]) ? "1" : "0");
}

static int pickleCommandLength(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2)
		return pickle_error_arity(i, 2, argc, argv);
	return pickle_set_result_integer(i, strlen(argv[1])/*strnlen(argv[2], PICKLE_MAX_STRING)*/);
}

static int pickleCommandRaise(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2)
		return pickle_error_arity(i, 2, argc, argv);
	return pickle_set_result_integer(i, raise(atoi(argv[1])));
}

static int signal_variable = 0;

static void signal_handler(int sig) {
	signal_variable = sig;
}

static int pickleCommandSignal(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 1 && argc != 3)
		return pickle_error_arity(i, 2, argc, argv);
	if (argc == 1) {
		const int sig = signal_variable;
		signal_variable = 0;
		return pickle_set_result_integer(i, sig);
	}
	int r = PICKLE_ERROR, sig = atoi(argv[1]);
	char *rq = argv[2];
	if (!strcmp(rq, "ignore"))  { r = SIG_ERR == signal(sig, SIG_IGN) ? r : PICKLE_OK; }
	if (!strcmp(rq, "default")) { r = SIG_ERR == signal(sig, SIG_DFL) ? r : PICKLE_OK; }
	if (!strcmp(rq, "catch"))   { r = SIG_ERR == signal(sig, signal_handler) ? r : PICKLE_OK; }
	if (pickle_set_result_integer(i, r == PICKLE_OK) != PICKLE_OK)
		return PICKLE_ERROR;
	return r;
}

static int pickleCommandHeapUsage(pickle_t *i, int argc, char **argv, void *pd) {
	pool_t *p = pd;
	long info = -1;

	if (argc > 3)
		return pickle_error_arity(i, 3, argc, argv);
	if (argc == 1) {
		info = !!p;
		goto done;
	}

	if (!p)
		return pickle_set_result_string(i, "unknown");
	const char *const rq = argv[1];
	if (argc == 2) {
		if      (!strcmp(rq, "freed"))    { info = p->freed; }
		else if (!strcmp(rq, "allocs"))   { info = p->allocs;  }
		else if (!strcmp(rq, "reallocs")) { info = p->relocations;  }
		else if (!strcmp(rq, "active"))   { info = p->active;  }
		else if (!strcmp(rq, "max"))      { info = p->max;    }
		else if (!strcmp(rq, "total"))    { info = p->total;  }
		else if (!strcmp(rq, "blocks"))   { info = p->blocks; }
		else if (!strcmp(rq, "arenas"))   { info = p->count; }
		else { /* do nothing */ }
	} else if (argc == 3) {
		const int pool = atoi(argv[2]);
		if ((pool >= 0) && (pool < (int)p->count)) {
			block_arena_t *a = p->arenas[pool];
			if      (!strcmp(rq, "arena-size"))   { info = a->freelist.bits; }
			else if (!strcmp(rq, "arena-block"))  { info = a->blocksz; }
			else if (!strcmp(rq, "arena-active")) { info = a->active; }
			else if (!strcmp(rq, "arena-max"))    { info = a->max; }
			else { /* do nothing */ }
		}
	}
done:
	return pickle_set_result_integer(i, info);
}

static void help(FILE *output, const char *arg0);

static int pickleCommandInfo(pickle_t *i, const int argc, char **argv, void *pd) {
	if (argc >= 2 && !strcmp(argv[1], "heap"))
		return pickleCommandHeapUsage(i, argc - 1, argv + 1, pd);
	if (argc != 2)
		return pickle_error_arity(i, 2, argc, argv);
	const char *rq = argv[1];
	if (!strcmp(rq, "level")) {
		int depth = 0;
		if (pickle_get_call_depth(i, &depth) != PICKLE_OK)
			return pickle_error(i, "Call depth exceeded: %d", depth);
		return pickle_set_result_integer(i, depth);
       	}
	if (!strcmp(rq, "line"))  {
		int line = 0;
		if (pickle_get_line_number(i, &line) != PICKLE_OK)
			line = -1;
		return pickle_set_result_integer(i, line);
	}
	if (!strcmp(rq, "width")) {
		return pickle_set_result_integer(i, CHAR_BIT*sizeof(char*));
	}
	return pickle_error(i, "Unknown info option '%s'", rq);
}

static int pickleCommandArgv(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(pd);
	char **global_argv = ((argument_t*)pd)->argv;
	const int global_argc = ((argument_t*)pd)->argc;
	if (argc != 1 && argc != 2)
		return pickle_error_arity(i, 2, argc, argv);
	if (argc == 1)
		return pickle_set_result_integer(i, global_argc);
	const int j = atoi(argv[1]);
	if (j < 0 || j >= global_argc)
		return pickle_set_result_string(i, "");
	else
		return pickle_set_result_string(i, global_argv[j]);
}

/* retrieve and process those pickles you filed away for safe keeping */
static int file(pickle_t *i, const char *name, FILE *output, int command) {
	assert(i);
	assert(file);
	assert(output);
	errno = 0;
	FILE *fp = fopen(name, "rb");
	if (!fp) {
		if (command)
			return pickle_error(i, "Failed to open file %s (rb): %s\n", name, strerror(errno));
		fprintf(stderr, "Failed to open file %s (rb): %s\n", name, strerror(errno));
		return -1;
	}
	char buf[FILE_SZ];
	buf[fread(buf, 1, FILE_SZ, fp)] = '\0';
	fclose(fp);
	int retcode = PICKLE_OK;
	if ((retcode = pickle_eval(i, buf)) != PICKLE_OK)
		if (!command) {
			const char *s = NULL;
			if (pickle_get_result_string(i, &s) != PICKLE_OK)
				return -1;
			fprintf(output, "%s\n", s);
		}
	return retcode == PICKLE_OK ? 0 : -1;
}

static int pickleCommandSource(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(i);
	assert(file);
	assert(pd);
	if (argc != 2)
		return pickle_error_arity(i, 2, argc, argv);
	if (file(i, argv[1], pd, 1) < 0)
		return PICKLE_ERROR;
	return PICKLE_OK;
}

static int register_custom_commands(pickle_t *i, argument_t *args, pool_t *p, int prompt) {
	assert(i);
	assert(args);
	const pickle_register_command_t commands[] = {
		{ "puts",     pickleCommandPuts,      stdout },
		{ "gets",     pickleCommandGets,      stdin },
		{ "error",    pickleCommandError,     stderr },
		{ "system",   pickleCommandSystem,    NULL },
		{ "exit",     pickleCommandExit,      NULL },
		{ "quit",     pickleCommandExit,      NULL },
		{ "bye",      pickleCommandExit,      NULL }, // hold over from Forth
		{ "getenv",   pickleCommandGetEnv,    NULL },
		{ "random",   pickleCommandRandom,    NULL },
		{ "clock",    pickleCommandClock,     NULL },
		{ "match",    pickleCommandMatch,     NULL },
		{ "eq",       pickleCommandEqual,     NULL },
		{ "ne",       pickleCommandNotEqual,  NULL },
		{ "length",   pickleCommandLength,    NULL },
		{ "raise",    pickleCommandRaise,     NULL },
		{ "signal",   pickleCommandSignal,    NULL },
		{ "argv",     pickleCommandArgv,      args },
		{ "source",   pickleCommandSource,    stdout },
		{ "info",     pickleCommandInfo,      p },
		{ "heap",     pickleCommandHeapUsage, p },
	};
	if (pickle_set_var_integer(i, "argc", args->argc) != PICKLE_OK)
		return -1;
	if (pickle_set_var_string(i, "prompt", prompt ? "pickle> " : "") != PICKLE_OK)
		return -1;
	for (size_t j = 0; j < sizeof(commands)/sizeof(commands[0]); j++)
		if (pickle_register_command(i, commands[j].name, commands[j].func, commands[j].data) != 0)
			return -1;
	return 0;
}

/* an interactive pickle - the things you can do with it! */
static int interactive(pickle_t *i, FILE *input, FILE *output) { /* NB. This could be rewritten as a script now */
	assert(i);
	assert(input);
	assert(output);
	for (;;) {
		char clibuf[LINE_SZ];
		const char *prompt = NULL;
		pickle_get_var_string(i, "prompt", &prompt);
		prompt = prompt ? prompt : "";
		fputs(prompt, output);
		fflush(output);
		if (!fgets(clibuf, sizeof clibuf, input))
			return 0;
		const int retcode = pickle_eval(i, clibuf);
		const char *s = NULL;
		if (pickle_get_result_string(i, &s) != PICKLE_OK)
			return -1;
		if (s[0] != '\0')
			fprintf(output, "[%d] %s\n", retcode, s);
	}
	return 0;
}

static int tests(void) {
	typedef int (*test_t)(void);
	static const test_t ts[] = { block_tests, pickle_tests, NULL };
	int r = 0;
	for (size_t i = 0; ts[i]; i++)
		 if (ts[i]() != 0)
			 r = -1;
	return r;
}

static void help(FILE *output, const char *arg0) {
	assert(arg0);
	static const char *msg = "\
Usage: %s file.tcl...\n\n\
Pickle:     A tiny TCL like language derived/copied from 'picol'\n\
License:    BSD (Antirez for original picol, Richard Howe for modifications)\n\
Repository: https://github.com/howerj/pickle\n\
\n\
Options:\n\
\n\
\t--           stop processing command line arguments\n\
\t-h, --help   display this help message and exit\n\
\t-t, --test   run built in self tests and exit (return code 0 is success)\n\
\t-a           use custom block allocator, for testing purposes\n\
\t-s, --silent suppress prompt printing\n\
\n\
If no arguments are given then input is taken from stdin. Otherwise\n\
they are treated as scripts to execute. Maximum file size is %d\n\
bytes, maximum length of an input line is is %d bytes. There are no\n\
configuration files or environment variables need by the interpreter.\n\
Non zero return codes indicate failure.\n";
	fprintf(output, msg, arg0, FILE_SZ, LINE_SZ);
}

static void cleanup(void) {
	static int cleaned = 0;
	if (cleaned)
		return;
	cleaned = 1;
	pickle_delete(interp);
	if (use_custom_allocator) {
		use_custom_allocator = 0;
		pool_delete(block_allocator.arena);
	}
}

int main(int argc, char **argv) {
	int r = 0, prompt_on = 1, j;
	argument_t args = { .argc = argc, .argv = argv };

	static const pool_specification_t specs[] = {
		{ 8,   512 }, /* most allocations are quite small */
		{ 16,  256 },
		{ 32,  128 },
		{ 64,   64 },
		{ 128,  32 },
		{ 256,  16 },
		{ 512,   8 }, /* maximum string length is bounded by this */
	};

	if (atexit(cleanup)) {
		fprintf(stderr, "atexit failed\n");
		return -1;
	}

	for (j = 1; j < argc; j++) {
		if (!strcmp(argv[j], "--")) {
			j++;
			break;
		} else if (!strcmp(argv[j], "-a")) {
			use_custom_allocator = 1;
		} else if (!strcmp(argv[j], "-s") || !strcmp(argv[j], "--silent")) {
			prompt_on = 0;
		} else if (!strcmp(argv[j], "-h") || !strcmp(argv[j], "--help")) {
			help(stdout, argv[0]);
			return 0;
		} else if (!strcmp(argv[j], "-t") || !strcmp(argv[j], "--test")) {
			return tests();
		} else {
			break;
		}
	}
	argc -= j;
	argv += j;

	if (use_custom_allocator)
		if (!(block_allocator.arena = pool_new(sizeof(specs)/sizeof(specs[0]), &specs[0]))) {
			fputs("memory pool allocation failure\n", stderr);
			return EXIT_FAILURE;
		}

	if ((r = pickle_new(&interp, use_custom_allocator ? &block_allocator : NULL)) != PICKLE_OK)
		goto end;
	if ((r = register_custom_commands(interp, &args, block_allocator.arena, prompt_on)) < 0)
		goto end;
	if (argc == 0) {
		r = interactive(interp, stdin, stdout);
	} else {
		for (j = 0; j < argc; j++)
			if ((r = file(interp, argv[j], stdout, 0)) != PICKLE_OK)
				break;
	}
end:
	cleanup();
	return r;
}

