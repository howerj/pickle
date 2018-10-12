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
#include <time.h>

#define LINE_SZ (1024)
#define FILE_SZ (1024 * 16)
#define UNUSED(X) ((void)(X))

typedef struct { int argc; char **argv; } argument_t;

static int pickle_set_result_int(pickle_t *i, long r) {
	char v[64];
	snprintf(v, sizeof v, "%ld", r);
	return pickle_set_result(i, v);
}

static int pickle_set_var_int(pickle_t *i, const char *name, long r) {
	char v[64];
	snprintf(v, sizeof v, "%ld", r);
	return pickle_set_var(i, name, v);
}

static int pickleCommandPuts(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(pd);
	if (argc != 2 && argc != 3)
		return pickle_arity_error(i, 3, argc, argv);
	int newline = 1;
	char *line = argv[1];
	if (argc == 3) {
		line = argv[2];
		if (!strcmp(argv[1], "-nonewline")) { newline = 0; }
		else return pickle_error(i, "Unknown puts command %s", argv[1]);
	}
	if(newline)
		fprintf((FILE*)pd, "%s\n", line);
	else
		fputs(line, (FILE*)pd);
	fflush((FILE*)pd);
	return PICKLE_OK;
}

static int pickleCommandGets(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(pd);
	if (argc != 1)
		return pickle_arity_error(i, 1, argc, argv);
	char buf[PICKLE_MAX_STRING] = { 0 };
	(void)/*ignore result*/fgets(buf, sizeof buf, (FILE*)pd);
	return pickle_set_result(i, buf);
}

static int pickleCommandSystem(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2 && argc != 1)
		return pickle_arity_error(i, 2, argc, argv);
	const int r = system(argc == 1 ? NULL : argv[1]);
	return pickle_set_result_int(i, r);
}

static int pickleCommandRandom(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 1 && argc != 2)
		return pickle_arity_error(i, 2, argc, argv);
	if (argc == 2) {
		srand(atol(argv[1]));
		return PICKLE_OK;
	}
	return pickle_set_result_int(i, rand());
}

static int pickleCommandExit(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2 && argc != 1)
		return pickle_arity_error(i, 2, argc, argv);
	const char *code = argc == 2 ? argv[1] : "0";
	exit(atoi(code));
	return PICKLE_OK;
}

static int pickleCommandGetEnv(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2)
		return pickle_arity_error(i, 2, argc, argv);
	const char *env = getenv(argv[1]);
	return pickle_set_result(i, env ? env : "");
}

static int pickleCommandClock(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc == 1) {
		const long t = (((double)(clock()) / (double)CLOCKS_PER_SEC) * 1000.0);
		return pickle_set_result_int(i, t);
	}
	if (argc != 2)
		return pickle_arity_error(i, 2, argc, argv);
	char buf[PICKLE_MAX_STRING] = { 0 };
	time_t rawtime;
	time(&rawtime);
	struct tm *timeinfo = gmtime(&rawtime);
	strftime(buf, sizeof buf, argv[1], timeinfo);
	return pickle_set_result(i, buf);
}

/*Based on: <http://c-faq.com/lib/regex.html>*/
static int match(const char *pat, const char *str, size_t depth) {
	if (!depth) return -1;
 again:
        switch (*pat) {
	case '\0': return !*str;
	case '*': { /* match any number of characters: normally '.*' */
		const int r = match(pat + 1, str, depth - 1);
		if(r) return r;
		if(!*(str++)) return 0;
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
		return pickle_arity_error(i, 3, argc, argv);
	const int r = match(argv[1], argv[2], PICKLE_MAX_RECURSION);
	if(r < 0)
		return pickle_error(i, "Regex error: %d", r);
	return pickle_set_result(i, r ? "1" : "0");
}

static int pickleCommandEqual(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 3)
		return pickle_arity_error(i, 3, argc, argv);
	return pickle_set_result(i, !strcmp(argv[1], argv[2]) ? "1" : "0");
}

static int pickleCommandNotEqual(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 3)
		return pickle_arity_error(i, 3, argc, argv);
	return pickle_set_result(i, strcmp(argv[1], argv[2]) ? "1" : "0");
}

static int pickleCommandLength(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2)
		return pickle_arity_error(i, 2, argc, argv);
	return pickle_set_result_int(i, strlen(argv[1])/*strnlen(argv[2], PICKLE_MAX_STRING)*/);
}

static int pickleCommandRaise(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 2)
		return pickle_arity_error(i, 2, argc, argv);
	return pickle_set_result_int(i, raise(atoi(argv[1])));
}

static int signal_variable = 0;

static void signal_handler(int sig) {
	signal_variable = sig;
}

static int pickleCommandSignal(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc != 1 && argc != 3)
		return pickle_arity_error(i, 2, argc, argv);
	if (argc == 1) {
		int sig = signal_variable;
		signal_variable = 0;
		return pickle_set_result_int(i, sig);
	}
	int r = PICKLE_ERR, sig = atoi(argv[1]);
	char *rq = argv[2];
	if (!strcmp(rq, "ignore"))  { r = SIG_ERR == signal(sig, SIG_IGN) ? r : PICKLE_OK; }
	if (!strcmp(rq, "default")) { r = SIG_ERR == signal(sig, SIG_DFL) ? r : PICKLE_OK; }
	if (!strcmp(rq, "catch"))   { r = SIG_ERR == signal(sig, signal_handler) ? r : PICKLE_OK; }
	if (pickle_set_result_int(i, r == PICKLE_OK) != PICKLE_OK)
		return PICKLE_ERR;
	return r;
}

static int pickleCommandHeapUsage(pickle_t *i, int argc, char **argv, void *pd) {
	pool_t *p = pd;
	long info = -1;
	if (argc > 3)
		return pickle_arity_error(i, 3, argc, argv);
	if(argc == 1) {
		info = !!pd;
		goto done;
	}

	if (!pd)
		return pickle_set_result(i, "unknown");
	const char *const rq = argv[1];

	if(argc == 2) {
		if(!strcmp(rq, "freed"))         { info = p->freed; }
		else if(!strcmp(rq, "allocs"))   { info = p->allocs;  }
		else if(!strcmp(rq, "reallocs")) { info = p->relocations;  }
		else if(!strcmp(rq, "active"))   { info = p->active;  }
		else if(!strcmp(rq, "max"))      { info = p->max;    }
		else if(!strcmp(rq, "total"))    { info = p->total;  }
		else if(!strcmp(rq, "blocks"))   { info = p->blocks; }
		else if(!strcmp(rq, "arenas"))   { info = p->count; }
		else { /* do nothing */ }
	} else if(argc == 3) {
		const int pool = atoi(argv[2]);
		if((pool >= 0) && (pool < (int)p->count)) {
			block_arena_t *a = p->arenas[pool];
			if(!strcmp(rq, "arena-size"))       { info = a->freelist.bits; }
			else if(!strcmp(rq, "arena-block")) { info = a->blocksz; }
			else if(!strcmp(rq, "arena-used"))  { 
				info = 0;
				const size_t bits = a->freelist.bits;
				for(size_t j = 0; j < bits; j++)
					if(bitmap_get(&a->freelist, j))
						info++;
			}
			else { /* do nothing */ }
		}
	}
done:
	return pickle_set_result_int(i, info);
}

static void help(FILE *output, const char *arg0);

static int pickleCommandInfo(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(pd);
	if (argc >= 2 && !strcmp(argv[1], "heap"))
		return pickleCommandHeapUsage(i, argc - 1, argv + 1, i->allocator.arena);
	if (argc != 2)
		return pickle_arity_error(i, 2, argc, argv);
	const char *rq = argv[1];
	if(!strcmp(rq, "level")) { return pickle_set_result_int(i, i->level); }
	if(!strcmp(rq, "line"))  { return pickle_set_result_int(i, i->line); }
	return pickle_error(i, "Unknown info option '%s'", rq);
}

static int pickleCommandHelp(pickle_t *i, const int argc, char **argv, void *pd) {
	UNUSED(i);
	UNUSED(argc);
	UNUSED(argv);
	help(pd, "pickle");
	static const char *tutorial = "\n\n";
	fputs(tutorial, pd);
	return PICKLE_OK;
}


static int pickleCommandArgv(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(pd);
	char **global_argv = ((argument_t*)pd)->argv;
	const int global_argc = ((argument_t*)pd)->argc;
	if(argc != 1 && argc != 2)
		return pickle_arity_error(i, 2, argc, argv);
	if(argc == 1)
		return pickle_set_result_int(i, global_argc);
	const int j = atoi(argv[1]);
	if(j < 0 || j >= global_argc)
		return pickle_set_result(i, "");
	else
		return pickle_set_result(i, global_argv[j]);
}

/* retrieve and process those pickles you filed away for safe keeping */
static int file(pickle_t *i, const char *name, FILE *output, int command) {
	assert(i);
	assert(file);
	assert(output);
	errno = 0;
	FILE *fp = fopen(name, "r"); /**@bug interpreter does not handle 'rb' mode */
	if (!fp) {
		if (command)
			return pickle_error(i, "Failed to open file %s (rb): %s\n", name, strerror(errno));
		fprintf(stderr, "Failed to open file %s (rb): %s\n", name, strerror(errno));
		return -1;
	}
	i->line = 1;
	char buf[FILE_SZ];
	buf[fread(buf, 1, FILE_SZ, fp)] = '\0';
	fclose(fp);
	int retcode = PICKLE_OK;
	if ((retcode = pickle_eval(i, buf)) != PICKLE_OK)
		if (!command)
			fprintf(output, "%s\n", i->result);
	return retcode == PICKLE_OK ? 0 : -1;
}

static int pickleCommandSource(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(i);
	assert(file);
	assert(pd);
	if (argc != 2)
		return pickle_arity_error(i, 2, argc, argv);
	if (file(i, argv[1], pd, 1) < 0)
		return PICKLE_ERR;
	return PICKLE_OK;
}

static int register_custom_commands(pickle_t *i, argument_t *args, int prompt) {
	assert(i);
	assert(args);
	const pickle_register_command_t commands[] = {
		{ "puts",     pickleCommandPuts,      stdout },
		{ "gets",     pickleCommandGets,      stdin },
		{ "system",   pickleCommandSystem,    NULL },
		{ "exit",     pickleCommandExit,      NULL },
		{ "quit",     pickleCommandExit,      NULL },
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
		{ "info",     pickleCommandInfo,      NULL },
		{ "help",     pickleCommandHelp,      stdout },
		{ "heap",     pickleCommandHeapUsage, i->allocator.arena },
	};
	if (pickle_set_var_int(i, "argc", args->argc) != PICKLE_OK)
		return -1;
	if (pickle_set_var(i, "prompt", prompt ? "pickle> " : "") != PICKLE_OK)
		return -1;
	for (size_t j = 0; j < sizeof(commands)/sizeof(commands[0]); j++)
		if(pickle_register_command(i, commands[j].name, commands[j].func, commands[j].data) != 0)
			return -1;
	return 0;
}

/* an interactive pickle - the things you can do with it! */
static int interactive(pickle_t *i, FILE *input, FILE *output) { /**@todo rewrite this a pickle program? */
	assert(i);
	assert(input);
	assert(output);
	i->line = 0;
	for(;;) {
		char clibuf[LINE_SZ];
		const char *prompt = pickle_get_var(i, "prompt");
		prompt = prompt ? prompt : "";
		fputs(prompt, output);
		fflush(output);
		if (!fgets(clibuf, sizeof clibuf, input))
			return 0;
		const int retcode = pickle_eval(i, clibuf);
		if (i->result[0] != '\0')
			fprintf(output, "[%d] %s\n", retcode, i->result);
	}
	return 0;
}

static int tests(void) {
	typedef int (*test_t)(void);
	static const test_t ts[] = { block_tests, pickle_tests, NULL };
	int r = 0;
	for (size_t i = 0; ts[i]; i++)
		 if(ts[i]() < 0)
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

void *custom_malloc(void *a, size_t length)           { return pool_malloc(a, length); }
void custom_free(void *a, void *v)                    { pool_free(a, v); }
void *custom_realloc(void *a, void *v, size_t length) { return pool_realloc(a, v, length); }

static int use_custom_allocator = 0;
static pickle_t interp = { .initialized = 0 };
static pickle_allocator_t allocator = {
	.free    = custom_free,
	.realloc = custom_realloc,
	.malloc  = custom_malloc,
	.arena   = NULL
};

static void cleanup(void) {
	if(interp.initialized)
		pickle_deinitialize(&interp);
	if (use_custom_allocator) {
		use_custom_allocator = 0;
		pool_delete(allocator.arena);
	}
}

int main(int argc, char **argv) {
	int r = 0, prompt_on = 1, j;
	argument_t args = { .argc = argc, .argv = argv };

	static const pool_specification_t specs[] = {
		{ 8,   512 }, /* most allocations are quite small */
		{ 16,  256 },
		{ 32,  128 },
		{ 64,   32 },
		{ 128,  16 },
		{ 256,   8 },
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
		allocator.arena = pool_new(sizeof(specs)/sizeof(specs[0]), &specs[0]);

	if ((r = pickle_initialize(&interp, use_custom_allocator ? &allocator : NULL)) < 0)
		goto end;
	if ((r = register_custom_commands(&interp, &args, prompt_on)) < 0)
		goto end;
	if (argc == 0) {
		r = interactive(&interp, stdin, stdout);
	} else {
		for (j = 0; j < argc; j++)
			if((r = file(&interp, argv[j], stdout, 0)) != PICKLE_OK)
				break;
	}
end:
	cleanup();
	return r;
}

