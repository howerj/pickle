/**@file block.c
 * @brief A simple block allocator
 * @copyright Richard James Howe (2018)
 * @license BSD
 *
 * This file contains a simple memory pool allocator, and contains three main
 * sections as well as some optional tests. The sections are a bitmap data
 * structure used to store the free list, an allocator that can return fixed
 * size blocks and an allocator that can return blocks from multiple different
 * allocators of varying block widths.
 *
 * There are some restrictions on the block sizes, counts and alignment. The
 * block sizes need to be a power of two. All memory used by the block
 * allocator must be aligned to the strictest alignment required by your
 * system.
 *
 * @bug If block count is not a multiple or the sizeof bitmap_unit_t then
 * not all memory can be allocated.
 *
 * NOTE: As the structures needed to define a memory pool are available in
 * the header it is possible to allocate pools statically, or even on the
 * stack, as you see fit. You do not have to use 'pool_new' to create a new
 * pool. */

#include "block.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#define FIND_BY_BIT (0) /* Slow, simply, find free bit by bit? */
#define STATISTICS  (1) /* Collect statistics on allocations? */
#define FALLBACK    (0) /* Fallback to malloc/free if we cannot allocate? */
#define RANDOM_FAIL (0) /* Simulate random failures? (pool allocations only) */
#define USE_ABORT   (0) /* Call abort() on an error, instead of returning a code */
#define BITS        (sizeof(bitmap_unit_t)*CHAR_BIT)
#define MASK        (BITS-1)
#define MIN(X, Y)   ((X) < (Y) ? (X) : (Y))
#define MAX(X, Y)   ((X) > (Y) ? (X) : (Y))
#define FAIL_PROBABILITY (RAND_MAX/1000)
#define FAIL_SEED   (1987)

size_t bitmap_units(size_t bits) {
	return bits/BITS + !!(bits & MASK);
}

static inline size_t bitmap_unit_index(size_t bits) {
	return bits/BITS;
}

size_t bitmap_bits(bitmap_t *b) {
	assert(b);
	return b->bits;
}

void bitmap_free(bitmap_t *b) {
	if (!b)
		return;
	free(b->map);
	free(b);
}

bitmap_t *bitmap_new(size_t bits) {
	const size_t length = bitmap_units(bits)*sizeof(bitmap_unit_t);
	assert(length < (SIZE_MAX/bits));
	assert((length + sizeof(bitmap_t)) > length);
	bitmap_t *r = calloc(sizeof(bitmap_t), 1);
	if (!r)
		return NULL;
	r->map = calloc(length + 1, sizeof(r->map[0]));
	if (!r->map) {
		free(r);
		return NULL;
	}
	r->bits = bits;
	return r;
}

bitmap_t *bitmap_copy(const bitmap_t *b) {
	assert(b);
	bitmap_t *r = bitmap_new(b->bits);
	if (!r)
		return NULL;
	memcpy(r->map, b->map, bitmap_units(b->bits) * sizeof(bitmap_unit_t));
	return r;
}

void bitmap_set(bitmap_t *b, size_t bit) {
	assert(b);
	assert(bit < b->bits);
	b->map[bit/BITS] |=  (1u << (bit & MASK));
}

void bitmap_clear(bitmap_t *b, size_t bit) {
	assert(b);
	assert(bit < b->bits);
	b->map[bit/BITS] &= ~(1u << (bit & MASK));
}

void bitmap_toggle(bitmap_t *b, size_t bit) {
	assert(b);
	assert(bit < b->bits);
	b->map[bit/BITS] ^=  (1u << (bit & MASK));
}

bool bitmap_get(bitmap_t *b, size_t bit) {
	assert(b);
	assert(bit < b->bits);
	return !!(b->map[bit/BITS] & (1u << (bit & MASK)));
}

static inline size_t block_count(block_arena_t *a) {
	assert(a);
	return bitmap_bits(&a->freelist);
}

/* NOTES: Speeding up this function increases the speed of allocation,
 * deallocation is very fast as it is just clearing a bit field, but for
 * allocation it the allocator has to find a free bit which can mean traversing
 * the entire bitfield. Some potential operations include keeping a stack of
 * values just recently freed (like keeping a free list), or micro
 * optimizations such as using a highly optimized version of 'memchr' if your
 * library provides one. */
static inline long block_find_free(block_arena_t *a) {
	assert(a);
	if (FIND_BY_BIT) { /* much slower, simpler */
		bitmap_t *b = &a->freelist;
		const long max = block_count(a);
		for (long i = 0; i < max; i++)
			if (!bitmap_get(b, i))
				return i;
		return -1;
	}
	if (a->lastfree) {
		const long r = a->lastfree;
		a->lastfree = 0;
		bitmap_get(&a->freelist, r);
		return r;
	}
	bitmap_t *b = &a->freelist;
	bitmap_unit_t *u = b->map;
	size_t max = bitmap_unit_index(b->bits), start = bitmap_unit_index(a->lastalloc);
	for (size_t c = 0, i = start; c <= max; i = (i + 1) % (max ? max : 1) /*& (max - 1)*/, c++)
		if (u[i] != (bitmap_unit_t)-1uLL) {
			const size_t index = i * BITS;
			const size_t end = MIN(b->bits, (index + BITS));
			for (size_t j = index; j < end; j++)
				if (!bitmap_get(b, j)) {
					a->lastalloc = j;
					return j;
				}
		}
	return -1;
}

static inline bool is_aligned(void *v) {
	assert(v);
	uintptr_t p = (uintptr_t)v;
	if (sizeof(p) == 2)
		return !(p & 1);
	if (sizeof(p) == 4)
		return !(p & 3);
	if (sizeof(p) == 8)
		return !(p & 7);
	return 0;
}

static bool is_power_of_2(unsigned long long x) {
	return x && !(x & (x - 1));
}

void *block_malloc(block_arena_t *a, size_t length) {
	assert(a);
	if (a->blocksz < length)
		return NULL;
	const long f = block_find_free(a);
	if (f < 0)
		return NULL;
	if (STATISTICS) {
		a->active++;
		if (a->max < a->active)
			a->max = a->active;
	}
	bitmap_set(&a->freelist, f);
	void *r = ((char*)a->memory) + (f * a->blocksz);
	assert(is_aligned(r));
	return r;
}

void *block_calloc(block_arena_t *a, size_t length) {
	assert(a);
	void *r = block_malloc(a, length);
	if (!r)
		return r;
	memset(r, 0, a->blocksz);
	return r;
}

static inline int block_arena_valid_pointer(block_arena_t *a, void *v) {
	assert(a);
	const size_t max = block_count(a);
	if (v < a->memory || (char*)v > ((char*)a->memory + (max * a->blocksz)))
		return 0;
	return 1;
}

int block_free(block_arena_t *a, void *v) {
	assert(a);
	if (!v)
		return 0;
	if (!block_arena_valid_pointer(a, v)) {
		if (USE_ABORT)
			abort();
		return -1;
	}
	const intptr_t p = ((char*)v - (char*)a->memory);
	const size_t bit = p / a->blocksz;
	if (!bitmap_get(&a->freelist, bit)) { /* double free */
		if (USE_ABORT)
			abort();
		return -1;
	}
	if (STATISTICS)
		a->active--;
	bitmap_clear(&a->freelist, bit);
	a->lastfree = bit;
	return 0;
}

void *block_realloc(block_arena_t *a, void *v, size_t length) {
	assert(a);
	if (!length) {
		block_free(a, v);
		return NULL;
	}
	if (!v)
		return block_malloc(a, length);
	if (length < a->blocksz)
		return v;
	return NULL;
}

void block_delete(block_arena_t *a) {
	if (!a)
		return;
	free(a->freelist.map);
	free(a->memory);
	free(a);
}

block_arena_t *block_new(size_t blocksz, size_t count) {
	block_arena_t *a = NULL;
	if (blocksz < sizeof(intptr_t))
		goto fail;
	if (!is_power_of_2(blocksz))
		goto fail;
	if (!(a = calloc(sizeof(*a), 1)))
		goto fail;
	a->freelist.map = calloc((count / sizeof(bitmap_unit_t)) + sizeof(bitmap_unit_t), 1);
	a->memory       = calloc(blocksz, count);
	if (!(a->freelist.map) || !(a->memory))
		goto fail;
	a->freelist.bits = count;
	a->blocksz = blocksz;
	return a;
fail:
	block_delete(a);
	return NULL;
}

void pool_delete(pool_t *p) {
	if (!p)
		return;
	if (p->tracer)
		p->tracer(p->tracer_arg, "{delete %p}", (void*)p);
	if (p->arenas)
		for (size_t i = 0; i < p->count; i++)
			block_delete(p->arenas[i]);
	free(p->arenas);
	free(p);
}

pool_t *pool_new(size_t length, const pool_specification_t *specs) {
	assert(specs);
	pool_t *p = calloc(sizeof *p, 1);
	if (!p)
		goto fail;
	p->count = length;
	p->arenas = calloc(sizeof(p->arenas[0]), p->count);
	if (!(p->arenas))
		goto fail;
	for (size_t i = 0; i < length; i++) {
		const pool_specification_t spec = specs[i];
		p->arenas[i] = block_new(spec.blocksz, spec.count);
		if (!(p->arenas[i]))
			goto fail;
	}
	if (RANDOM_FAIL)
		srand(FAIL_SEED);
	return p;
fail:
	pool_delete(p);
	return NULL;
}

void *pool_malloc(pool_t *p, size_t length) {
	assert(p);
	void *r = NULL;
	if (RANDOM_FAIL)
		if (rand() < FAIL_PROBABILITY) {
			if (p->tracer)
				p->tracer(p->tracer_arg, "{malloc %p: random fail}", (void*)p);
			return NULL;
		}
	if (STATISTICS)
		p->allocs++, p->total += length;
	for (size_t i = 0; i < p->count; i++)
		if ((r = block_malloc(p->arenas[i], length))) {
			if (STATISTICS) {
				const size_t bsz = p->arenas[i]->blocksz;
				p->active += bsz;
				p->blocks += bsz;
				if (p->max < p->active)
					p->max = p->active;
			}
			goto end;
		}
	if (FALLBACK)
		r = malloc(length);
end:
	if (p->tracer)
		p->tracer(p->tracer_arg, "{malloc %p: %p %6zu}", (void*)p, r, length);
	return r;
}

void *pool_calloc(pool_t *p, size_t length) {
	void *r = pool_malloc(p, length);
	return r ? memset(r, 0, length) : r;
}

int pool_free(pool_t *p, void *v) {
	assert(p);
	if (p->tracer)
		p->tracer(p->tracer_arg, "{free   %p: %p}", (void*)p, v);
	if (!v)
		return 0;
	if (STATISTICS)
		p->freed++;
	for (size_t i = 0; i < p->count; i++) {
		if (block_arena_valid_pointer(p->arenas[i], v)) {
			if (STATISTICS)
				p->active -= p->arenas[i]->blocksz;
			return block_free(p->arenas[i], v);
		}
	}
	if (FALLBACK) {
		free(v);
		return 0;
	}
	if (USE_ABORT)
		abort();
	return -1;
}

size_t pool_block_size(pool_t *p, void *v) {
	assert(p);
	for (size_t i = 0; i < p->count; i++)
		if (block_arena_valid_pointer(p->arenas[i], v))
			return p->arenas[i]->blocksz;
	if (USE_ABORT)
		abort();
	return 0; /*WARNING: Returns zero! Which is kind-of and invalid value... */
}

static inline bool pool_valid_pointer(pool_t *p, void *v) {
	assert(p);
	for (size_t i = 0; i < p->count; i++)
		if (block_arena_valid_pointer(p->arenas[i], v))
			return true;
	return false;
}

void *pool_realloc(pool_t *p, void *v, size_t length) {
	assert(p);
	if (STATISTICS)
		p->relocations++;
	if (!length) {
		pool_free(p, v);
		return NULL;
	}
	if (!v)
		return pool_malloc(p, length);
	if (FALLBACK)
		if (!pool_valid_pointer(p, v))
			return realloc(v, length);
	const size_t oldsz = pool_block_size(p, v);
	const size_t minsz = MIN(oldsz, length);
	assert(oldsz != 0);
	if (length > (oldsz/2) && length < oldsz)
		return v;
	void *n = pool_malloc(p, length);
	if (!n)
		return NULL;
	memcpy(n, v, minsz);
	pool_free(p, v);
	return n;
}

#ifdef NDEBUG
int block_tests(void) { return 0; }
#else
#include <stdio.h>

#define BLK_COUNT (16)
#define BLK_SIZE  (32) /* must be power of 2! */

BLOCK_DECLARE(block_arena, BLK_COUNT, BLK_SIZE);

static uintptr_t diff(void *a, void *b) {
	assert(a);
	assert(b);
	return a > b ? (char*)a - (char*)b : (char*)b - (char*)a;
}

int block_tests(void) {
	void *v1, *v2, *v3;
	if (!(v1 = block_malloc(&block_arena, 12)))
		return -1;
	if (!(v2 = block_malloc(&block_arena, 30)))
		return -2;
	if (diff(v1, v2) != BLK_SIZE)
		return -3;
	block_free(&block_arena, v1);
	v1 = block_malloc(&block_arena, 12);
	if (!(v3 = block_malloc(&block_arena, 12)))
		return -4;
	if (diff(v1, v3) != BLK_SIZE*2)
		return -5;
	block_free(&block_arena, v1);
	block_free(&block_arena, v2);
	block_free(&block_arena, v3);
	size_t i = 0;
	for (i = 0; i < (BLK_COUNT+1); i++)
		if (!block_malloc(&block_arena, 1))
			break;
	if (i != BLK_COUNT)
		return -6;
	return 0;
}
#endif
