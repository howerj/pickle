/**@file main.c
 * @brief Extensions and driver for the 'pickle' interpreter. The
 * interpreter is a copy and modification of the 'picol' interpreter
 * by antirez. See the 'pickle.h' header for more information.
 * @author Richard James Howe
 * @license BSD 
 * TODO: Use PICKLE_ERROR/PICKLE_OK throughout this program instead of 0/-1
 * TODO: Change PICKLE_ERROR so it is '-1', not '1'. */
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
#include <ctype.h>
#include <stdarg.h>

#define LINE_SZ   (1024)      /* super lazy: maximum size of a line */
#define FILE_SZ   (1024 * 16) /* super lazy: maximum size of file to interpret */
#define UNUSED(X) ((void)(X))

typedef struct {
	const char *name;
	pickle_command_func_t func;
	void *data;
} pickle_register_command_t;

typedef struct { int argc; char **argv; } argument_t;

static int use_custom_allocator = 0;
static pickle_t *interp = NULL;
static int signal_variable = 0;

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
		return pickle_set_result_error_arity(i, 3, argc, argv);
	int newline = 1;
	const char *line = argv[1];
	if (argc == 3) {
		line = argv[2];
		if (!strcmp(argv[1], "-nonewline")) { newline = 0; }
		else return pickle_set_result_error(i, "Unknown puts command %s", argv[1]);
	}
	const int r1 = newline ? fprintf((FILE*)pd, "%s\n", line) : fputs(line, (FILE*)pd);
	const int r2 = fflush((FILE*)pd);
	if (r1 < 0 || r2 < 0)
		return pickle_set_result_error(i, "I/O error: %d/%d", r1, r2);
	return PICKLE_OK;
}

static int pickleCommandGets(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(pd);
	if (argc != 1)
		return pickle_set_result_error_arity(i, 1, argc, argv);
	char buf[PICKLE_MAX_STRING] = { 0 };
	if (!fgets(buf, sizeof buf, (FILE*)pd)) {
		pickle_set_result_string(i, "EOF");
		return PICKLE_ERROR;
	}
	return pickle_set_result_string(i, buf);
}

static int pickleCommandError(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(pd);
	if (argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	const int r1 = fprintf((FILE*)pd, "%s\n", argv[1]);
	const int r2 = fflush((FILE*)pd);
	if (r1 < 0 || r2 < 0)
		return pickle_set_result_error(i, "I/O error: %d/%d", r1, r2);
	return PICKLE_ERROR;
}

static int pickleCommandSystem(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(!pd);
	UNUSED(pd);
	if (argc != 2 && argc != 1)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	const int r = system(argc == 1 ? NULL : argv[1]);
	return pickle_set_result_integer(i, r);
}

static int pickleCommandRandom(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(!pd);
	UNUSED(pd);
	if (argc != 1 && argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	if (argc == 2) {
		srand(atol(argv[1]));
		return PICKLE_OK;
	}
	return pickle_set_result_integer(i, rand());
}

static int pickleCommandExit(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(!pd);
	UNUSED(pd);
	if (argc != 2 && argc != 1)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	const char *code = argc == 2 ? argv[1] : "0";
	exit(atoi(code));
	return PICKLE_OK;
}

static int pickleCommandGetEnv(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(!pd);
	UNUSED(pd);
	if (argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	const char *env = getenv(argv[1]);
	return pickle_set_result_string(i, env ? env : "");
}

static int pickleCommandClock(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(!pd);
	UNUSED(pd);
	if (argc == 1) {
		const long t = (((double)(clock()) / (double)CLOCKS_PER_SEC) * 1000.0);
		return pickle_set_result_integer(i, t);
	}
	if (argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	char buf[PICKLE_MAX_STRING] = { 0 };
	time_t rawtime;
	time(&rawtime);
	struct tm *timeinfo = gmtime(&rawtime);
	strftime(buf, sizeof buf, argv[1], timeinfo);
	return pickle_set_result_string(i, buf);
}

static int pickleCommandEqual(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(!pd);
	UNUSED(pd);
	if (argc != 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	return pickle_set_result_string(i, !strcmp(argv[1], argv[2]) ? "1" : "0");
}

static int pickleCommandNotEqual(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(!pd);
	UNUSED(pd);
	if (argc != 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	return pickle_set_result_string(i, strcmp(argv[1], argv[2]) ? "1" : "0");
}

static int pickleCommandRaise(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(!pd);
	UNUSED(pd);
	if (argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	return pickle_set_result_integer(i, raise(atoi(argv[1])));
}

static int pickleCommandGetCh(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(pd);
	FILE *f = pd;
	if (argc != 1)
		return pickle_set_result_error_arity(i, 1, argc, argv);
	return pickle_set_result_integer(i, fgetc(f));
}

static int pickleCommandPutCh(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(pd);
	FILE *f = pd;
	if (argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	return pickle_set_result_integer(i, fputc(atoi(argv[1]), f));
}

static void signal_handler(int sig) {
	signal_variable = sig;
}

static int pickleCommandSignal(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(!pd);
	UNUSED(pd);
	if (argc != 1 && argc != 3)
		return pickle_set_result_error_arity(i, 2, argc, argv);
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

static void memory_tracer(void *file, const char *fmt, ...) {
	assert(file);
	assert(fmt);
	FILE *out = file;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(out, fmt, ap);
	va_end(ap);
	fputc('\n', out);
}

static int pickleCommandHeapUsage(pickle_t *i, int argc, char **argv, void *pd) {
	assert(pd || !pd); /* a neat way of saying 'may or may not be NULL */
	pool_t *p = pd;
	long info = -1;

	if (argc > 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
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
		else if (!strcmp(rq, "tron"))     { p->tracer = memory_tracer; p->tracer_arg = stdout; return PICKLE_OK; }
		else if (!strcmp(rq, "troff"))    { p->tracer = NULL; p->tracer_arg = NULL; return PICKLE_OK; }
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

static int pickleCommandArgv(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(pd);
	char **global_argv = ((argument_t*)pd)->argv;
	const int global_argc = ((argument_t*)pd)->argc;
	if (argc != 1 && argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
	if (argc == 1)
		return pickle_set_result_integer(i, global_argc);
	const int j = atoi(argv[1]);
	if (j < 0 || j >= global_argc)
		return pickle_set_result_string(i, "");
	else
		return pickle_set_result_string(i, global_argv[j]);
}

static char *slurp(FILE *input) { /* TODO: Add as function to interpreter */
	assert(input);
	char *r = NULL;
	if (fseek(input, 0, SEEK_END) < 0)
		goto fail;
	const long pos = ftell(input);
	if (pos < 0)
		goto fail;
	if (fseek(input, 0, SEEK_SET) < 0)
		goto fail;
	if (!(r = malloc(pos + 1))) /* TODO: Allow for custom allocator */
		goto fail;
	if (pos != (long)fread(r, 1, pos, input))
		goto fail;
	r[pos] = '\0'; /* Ensure NUL termination */
	rewind(input);
	return r;
fail:
	free(r);
	rewind(input);
	return NULL;
}

static char *slurp_by_name(const char *name) {
	assert(name);
	FILE *input = fopen(name, "rb");
	if (!input)
		return NULL;
	char *r = slurp(input);
	fclose(input);
	return r;
}

/* Retrieve and process those pickles you filed away for safe keeping */
static int file(pickle_t *i, const char *name, FILE *output, int command) {
	assert(i);
	assert(file);
	assert(output);
	errno = 0;
	char *program = slurp_by_name(name);
	if (!program) {
		if (command)
			return pickle_set_result_error(i, "Failed to open file %s (rb): %s\n", name, strerror(errno));
		fprintf(stderr, "Failed to open file %s (rb): %s\n", name, strerror(errno));
		return -1;
	}
	const int retcode = pickle_eval(i, program);
	if (retcode != PICKLE_OK)
		if (!command) {
			const char *s = NULL;
			if (pickle_get_result_string(i, &s) != PICKLE_OK) {
				free(program);
				return -1;
			}
			fprintf(output, "%s\n", s);
		}
	free(program);
	return retcode == PICKLE_OK ? 0 : -1;
}

static int pickleCommandSource(pickle_t *i, const int argc, char **argv, void *pd) {
	assert(i);
	assert(file);
	assert(pd);
	if (argc != 2)
		return pickle_set_result_error_arity(i, 2, argc, argv);
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
		{ "eq",       pickleCommandEqual,     NULL },
		{ "ne",       pickleCommandNotEqual,  NULL },
		{ "raise",    pickleCommandRaise,     NULL },
		{ "signal",   pickleCommandSignal,    NULL },
		{ "argv",     pickleCommandArgv,      args },
		{ "source",   pickleCommandSource,    stdout },
		{ "heap",     pickleCommandHeapUsage, p },
		{ "putch",    pickleCommandPutCh,     stdout },
		{ "getch",    pickleCommandGetCh,     stdin },
		{ "string",   pickle_command_string,  NULL },
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

/* An interactive pickle - the things you can do with it! */
static int interactive(pickle_t *i, FILE *input, FILE *output) { /* NB. This could be rewritten as a script now */
	assert(i);
	assert(input);
	assert(output);
	for (;;) {
		char clibuf[LINE_SZ] = { 0 };
		const char *prompt = NULL;
		/* TODO: Evaluate prompt variable, making it more flexible */
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
	assert(output);
	assert(arg0);
	static const char *msg = "\
Usage: %s file.tcl...\n\n\
Pickle:     A tiny TCL like language derived/copied from 'picol'\n\
License:    BSD (Antirez for original picol, Richard Howe for modifications)\n\
Repository: https://github.com/howerj/pickle\n\
\n\
Options:\n\
\n\
\t--,\tstop processing command line arguments\n\
\t-h,\tdisplay this help message and exit\n\
\t-t,\trun built in self tests and exit (return code 0 is success)\n\
\t-a,\tuse custom block allocator, for testing purposes\n\
\t-A,\tenable debugging of the custom allocator, implies '-a'\n\
\t-s,\tsuppress prompt printing\n\
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
	pickle_getopt_t opt = { .init = 0 };
	int r = 0, prompt_on = 1, memory_debug = 0, ch;
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

	while ((ch = pickle_getopt(&opt, argc, argv, "hatsA")) != PICKLE_RETURN) {
		switch (ch) {
		case 'A': memory_debug = 1; /* fall through */
		case 'a': use_custom_allocator = 1; break;
		case 's': prompt_on = 0; break;
		case 'h': help(stdout, argv[0]); return 0;
		case 't': return tests();
		default: help(stderr, argv[0]); return -1;
		}
	}

	if (use_custom_allocator) {
		pool_t *p = pool_new(sizeof(specs) / sizeof(specs[0]), &specs[0]);
		if (!(block_allocator.arena = p)) {
			fputs("memory pool allocation failure\n", stderr);
			return EXIT_FAILURE;
		} else {
			if (memory_debug) {
				p->tracer     = memory_tracer;
				p->tracer_arg = stdout;
			}
		}
	}

	if ((r = pickle_new(&interp, use_custom_allocator ? &block_allocator : NULL)) != PICKLE_OK)
		goto end;
	if ((r = register_custom_commands(interp, &args, block_allocator.arena, prompt_on)) < 0)
		goto end;
	if (argc == opt.index) {
		r = interactive(interp, stdin, stdout);
	} else {
		for (int j = opt.index; j < argc; j++)
			if ((r = file(interp, argv[j], stdout, 0)) != PICKLE_OK)
				break;
	}
end:
	cleanup();
	return r;
}

