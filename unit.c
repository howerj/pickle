#include "pickle.h"
#include "block.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define LINESZ    (1024u)
#define PROGSZ    (1024u * 64u * 2u)
#define UNUSED(X) ((void)(X))

#ifndef USE_POOL_ALLOC
#define USE_POOL_ALLOC  (0)
#endif

static void *custom_malloc(void *a, size_t length)           { return pool_malloc(a, length); }
static int   custom_free(void *a, void *v)                   { return pool_free(a, v); }
static void *custom_realloc(void *a, void *v, size_t length) { return pool_realloc(a, v, length); }

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

static int evalFile(pickle_t *i, FILE *f) {
	static char program[PROGSZ] = { 0 }; /* !! */
	program[fread(program, 1, PROGSZ, f)] = '\0';
	const int err = pickle_eval(i, program);
	if (err != PICKLE_OK) {
		const char *r = NULL;
		pickle_get_result_string(i, &r);
		fprintf(stdout, "%s\n", r);
		return PICKLE_ERROR;
	}
	return PICKLE_OK;
}

static void heapTracer(void *file, const char *fmt, ...) {
	FILE *out = file;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(out, fmt, ap);
	va_end(ap);
	fputc('\n', out);
}

static int commandHeap(pickle_t *i, int argc, char **argv, void *pd) {
	pool_t *p = pd;
	long info = PICKLE_ERROR;
	const char *rq = NULL;

	if (argc > 3)
		return pickle_set_result_error_arity(i, 3, argc, argv);
	if (argc == 1) {
		info = !!p;
		goto done;
	}

	if (!p)
		return pickle_set_result_string(i, "unknown");
	rq = argv[1];
	if (argc == 2) {
		if      (!strcmp(rq, "freed"))    { info = p->freed; }
		else if (!strcmp(rq, "allocs"))   { info = p->allocs;  }
		else if (!strcmp(rq, "reallocs")) { info = p->relocations;  }
		else if (!strcmp(rq, "active"))   { info = p->active;  }
		else if (!strcmp(rq, "max"))      { info = p->max;    }
		else if (!strcmp(rq, "total"))    { info = p->total;  }
		else if (!strcmp(rq, "blocks"))   { info = p->blocks; }
		else if (!strcmp(rq, "arenas"))   { info = p->count; }
		else if (!strcmp(rq, "tron"))     { p->tracer = heapTracer; p->tracer_arg = stdout; return PICKLE_OK; }
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

int main(int argc, char **argv) {

	const pool_specification_t specs[] = {
		{ 8,   512 }, /* most allocations are quite small */
		{ 16,  256 },
		{ 32,  128 },
		{ 64,   64 },
		{ 128,  32 },
		{ 256,  16 },
		{ 512,   8 }, /* maximum string length is bounded by this */
	};

	pickle_allocator_t block_allocator = {
		.free    = custom_free,
		.realloc = custom_realloc,
		.malloc  = custom_malloc,
		.arena   = NULL
	};

	pickle_allocator_t *a = USE_POOL_ALLOC ? &block_allocator : NULL;
	if (USE_POOL_ALLOC) {
		pool_t *p = pool_new(sizeof(specs) / sizeof(specs[0]), &specs[0]);
		if (!(a->arena = p)) {
			(void)fputs("memory pool allocation failure\n", stderr);
			return EXIT_FAILURE;
		}
	}

	pickle_t *i = NULL;
	if (pickle_tests()    != PICKLE_OK) goto fail;
	if (pickle_new(&i, a) != PICKLE_OK) goto fail;
	if (pickle_set_argv(i, argc, argv) != PICKLE_OK) goto fail;
	if (pickle_register_command(i, "gets",   commandGets,   stdin)    != PICKLE_OK) goto fail;
	if (pickle_register_command(i, "puts",   commandPuts,   stdout)   != PICKLE_OK) goto fail;
	if (pickle_register_command(i, "getenv", commandGetEnv, NULL)     != PICKLE_OK) goto fail;
	if (pickle_register_command(i, "exit",   commandExit,   NULL)     != PICKLE_OK) goto fail;
	if (pickle_register_command(i, "heap",   commandHeap,   a ? a->arena : NULL) != PICKLE_OK) goto fail;

	int r = 0;
	for (int j = 1; j < argc; j++) {
		FILE *f = fopen(argv[j], "rb");
		if (!f) {
			(void)fprintf(stderr, "Invalid file %s\n", argv[j]);
			goto fail;
		}
		const int r = evalFile(i, f);
		fclose(f);
		if (r != PICKLE_OK)
			break;
	}
	if (argc == 1)
		r = evalFile(i, stdin);
	return !!pickle_delete(i) || r != PICKLE_OK;
fail:
	pickle_delete(i);
	return 1;
}
