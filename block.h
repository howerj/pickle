/**@file block.h
 * @brief Memory pool and bitmap related functions
 * @license BSD (2 clause)
 * @copyright Richard James Howe (2018)
 *
 * Copyright (c) 2018, Richard James Howe <howe.r.j.89@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE. */

#ifndef BLOCK_H
#define BLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>

typedef unsigned bitmap_unit_t;
typedef struct {
	size_t bits;
	bitmap_unit_t *map;
} bitmap_t;

typedef struct {
	bitmap_t freelist; /* list of free blocks */
	size_t blocksz;    /* size of a block: 1, 2, 4, 8, ... */
	size_t lastalloc, lastfree;   /* last freed block */
	void *memory;      /* memory backing this allocator, should be aligned! */
	long active, max;  /* current active, maximum on heap at any one time */
} block_arena_t;

typedef void (*pool_tracer_func_t)(void *v, const char *fmt, ...);

typedef struct {
	size_t count;
	block_arena_t **arenas;

	/* statistics collection */
	long freed, allocs, relocations; /* non NULL frees, malloc/callocs, reallocs */
	long active, max; /* current active, maximum on heap at any one time */
	long total, blocks; /* total memory allocations, total memory allocations of blocks */
	/* common sense: do not allocate anything in the tracer... */
	pool_tracer_func_t tracer; /* optional tracing routine; if NULL, tracing is turned off */
	void *tracer_arg; /* passed to tracing routine, if used */
} pool_t;

typedef struct {
	size_t blocksz;
	size_t count;
} pool_specification_t;

size_t bitmap_units(size_t bits);
size_t bitmap_bits(bitmap_t *b);
bitmap_t *bitmap_new(size_t bits);
bitmap_t *bitmap_copy(const bitmap_t *b);
void bitmap_free(bitmap_t *b);
void bitmap_set(bitmap_t *b, size_t bit);
void bitmap_clear(bitmap_t *b, size_t bit);
void bitmap_toggle(bitmap_t *b, size_t bit);
bool bitmap_get(bitmap_t *b, size_t bit);

block_arena_t *block_new(size_t blocksz, size_t count); /* count should be divisible by bitmap_unit_t */
void block_delete(block_arena_t *a);

void *block_malloc(block_arena_t *a, size_t length);
void *block_calloc(block_arena_t *a, size_t length);
int block_free(block_arena_t *a, void *v);
void *block_realloc(block_arena_t *a, void *v, size_t length);

pool_t *pool_new(size_t count, const pool_specification_t *specs);
void pool_delete(pool_t *p);
void *pool_malloc(pool_t *p, size_t length);
int pool_free(pool_t *p, void *v);
void *pool_realloc(pool_t *p, void *v, size_t length);
void *pool_calloc(pool_t *p, size_t length);

#define BLOCK_DECLARE(NAME, BLOCK_COUNT, BLOCK_SIZE)\
	block_arena_t NAME = {\
		.freelist = {\
			.bits = BLOCK_COUNT,\
			.map  = (bitmap_unit_t [BLOCK_COUNT/sizeof(bitmap_unit_t) + !(BLOCK_COUNT/sizeof(bitmap_unit_t))]) { 0 }\
		},\
		.blocksz = BLOCK_SIZE,\
		.memory  = (void*)((uint64_t [BLOCK_COUNT * ((BLOCK_SIZE / sizeof(uint64_t)) + !(BLOCK_COUNT/sizeof(bitmap_unit_t)))]) { 0 }),\
		.active  = 0,\
		.max     = 0,\
	}

int block_tests(void);

#ifdef __cplusplus
}
#endif

#endif
