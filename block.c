/**@file block.c
 * @brief A simple block allocator
 * @copyright Richard James Howe (2018)
 * @license MIT 
 *
 * @todo Optimize, cleanup and improve. Write better unit tests. */

#include "block.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <stdio.h>

#define BITS (sizeof(bitmap_unit_t)*CHAR_BIT)
#define MASK (BITS-1)

size_t bitmap_units(size_t bits) {
	return bits/BITS + !!(bits & MASK);
}

size_t bitmap_bits(bitmap_t *b) {
	assert(b);
	return b->bits;
}

bitmap_t *bitmap_new(size_t bits) {
	const size_t length = bitmap_units(bits)*sizeof(bitmap_unit_t);
	assert(length < (SIZE_MAX/bits));
	assert((length + sizeof(bitmap_t)) > length);
	bitmap_t *r = calloc(sizeof(bitmap_t), 1);
	if(!r)
		return NULL;
	r->map = calloc(length, sizeof(r->map[0]));
	if(!r->map) {
		free(r);
		return NULL;
	}
	r->bits = bits;
	return r;
}

bitmap_t *bitmap_copy(const bitmap_t *b) {
	assert(b);
	bitmap_t *r = bitmap_new(b->bits);
	if(!r)
		return NULL;
	memcpy(r->map, b->map, bitmap_units(b->bits) * sizeof(bitmap_unit_t));
	return r;
}

void bitmap_free(bitmap_t *b) {
	if(!b)
		return;
	free(b->map);
	free(b);
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

static size_t block_count(block_arena_t *a) {
	assert(a);
	return bitmap_bits(&a->freelist);
}

#define FIND_BY_BIT (0)

static long block_find_free(block_arena_t *a) {
	assert(a);
	if(FIND_BY_BIT) { /* much slower, simpler */
		bitmap_t *b = &a->freelist;
		long r = -1, max = block_count(a);
		for(long i = 0; i < max; i++)
			if(!bitmap_get(b, i))
				return i;
		return r;
	}
	bitmap_t *b = &a->freelist;
	bitmap_unit_t *u = b->map;
	long r = -1;
	const long max = bitmap_units(b->bits), start = 0; // start = bitmap_units(a->lastfree);
	/*assert(start <= max);
	if(u[start] == (bitmap_unit_t)-1uLL)
		a->lastfree = 0;*/
	for(long i = start; i < max; i++)
		if(u[i] != (bitmap_unit_t)-1uLL) {
			for(long j = i*BITS; j < (long)((i*BITS)+BITS); j++)
				if(!bitmap_get(b, j))
					return j;
			abort(); /* should never be reached */
		}
	return r;
}

static inline bool is_aligned(void *v) {
	uintptr_t p = (uintptr_t)v;
	if(sizeof(p) == 2)
		return !(p & 1);
	if(sizeof(p) == 4)
		return !(p & 3);
	if(sizeof(p) == 8)
		return !(p & 7);
	return 0;
}

bool is_power_of_2(unsigned long long x) {
	return x && !(x & (x - 1));
}

/**@todo use last free */
void *block_arena_malloc_block(block_arena_t *a, size_t length) {
	assert(a);
	assert(is_power_of_2(a->blocksz));
	assert((block_count(a) % sizeof(bitmap_unit_t)) == 0);
	if(a->blocksz < length)
		return NULL;
	long f = block_find_free(a);
	if(f < 0)
		return NULL;
	bitmap_set(&a->freelist, f);
	void *r = ((char*)a->memory) + (f * a->blocksz);
	assert(is_aligned(r));
	//fprintf(stderr, "%u\n", (unsigned)length);
	return r;
}

void *block_arena_calloc_block(block_arena_t *a, size_t length) {
	void *r = block_arena_malloc_block(a, length);
	if(!r)
		return r;
	memset(r, 0, a->blocksz);
	return r;
}

int block_arena_valid_pointer(block_arena_t *a, void *v) {
	const size_t max = block_count(a);
	if(v < a->memory || (char*)v > ((char*)a->memory + (max * a->blocksz))) 
		return 0;
	return 1;
}

void block_arena_free_block(block_arena_t *a, void *v) {
	assert(a);
	if(!v)
		return;
	if(!block_arena_valid_pointer(a, v)) {
		abort();
		return;
	}
	const intptr_t p = ((char*)v - (char*)a->memory);
	const size_t bit = p / a->blocksz;
	if(!bitmap_get(&a->freelist, bit)) { /* double free */
		abort();
		return;
	}
	bitmap_clear(&a->freelist, bit);
	a->lastfree = bit;
}

void *block_arena_realloc_block(block_arena_t *a, void *v, size_t length) {
	assert(a);
	if(!length) {
		block_arena_free_block(a, v);
		return NULL;
	}
	if(!v)
		return block_arena_malloc_block(a, length);
	if(length < a->blocksz)
		return v;
	return NULL;
}

block_arena_t *block_arena_new(size_t blocksz, size_t count) {
	block_arena_t *a = NULL;
	if(!is_power_of_2(blocksz) || !is_power_of_2(count))
		goto fail;
	if(!(a = calloc(sizeof(*a), 1)))
		goto fail;
	a->freelist.map = calloc(count / sizeof(bitmap_unit_t), 1);
	a->memory       = calloc(blocksz, count);
	if(!(a->freelist.map) || !(a->memory))
		goto fail;
	a->freelist.bits = count;
	a->blocksz = blocksz;
	return a;
fail:
	if(a) {
		free(a->freelist.map);
		free(a->memory);
	}
	free(a);
	return NULL;
}

void block_arena_delete(block_arena_t *a) {
	if(!a)
		return;
	free(a->freelist.map);
	free(a->memory);
	free(a);
}

pool_t *pool_new(void) { /**@todo handle errors, allow custom allocations */
	pool_t *p = malloc(sizeof *p);
	p->count = 3;
	p->arenas = malloc(sizeof(p->arenas[0]) * p->count);
	p->arenas[0] = block_arena_new(8,   1024);
	p->arenas[1] = block_arena_new(64,  128);
	p->arenas[2] = block_arena_new(512, 16);
	return p;
}

void pool_delete(pool_t *p) {
	if(!p)
		return;
	if(p->arenas)
		for(size_t i = 0; i < p->count; i++)
			block_arena_delete(p->arenas[i]);
	free(p->arenas);
	free(p);
}

void *pool_malloc(pool_t *p, size_t length) {
	assert(p);
	void *r = NULL;
	for(size_t i = 0; i < p->count; i++)
		if((r = block_arena_malloc_block(p->arenas[i], length)))
			return r;
	return r;
}

void pool_free(pool_t *p, void *v) {
	assert(p);
	if(!v)
		return;
	for(size_t i = 0; i < p->count; i++) {
		if(block_arena_valid_pointer(p->arenas[i], v)) {
			block_arena_free_block(p->arenas[i], v);
			return;
		}
	}
	abort();
}

size_t pool_block_size(pool_t *p, void *v) {
	assert(p);
	for(size_t i = 0; i < p->count; i++)
		if(block_arena_valid_pointer(p->arenas[i], v))
			return p->arenas[i]->blocksz;
	abort();
	return 0;
}

void *pool_realloc(pool_t *p, void *v, size_t length) {
	assert(p);
	if(!length) {
		pool_free(p, v);
		return NULL;
	}
	if(!v)
		return pool_malloc(p, length);
	void *n = pool_malloc(p, length);
	if(!n)
		return NULL;
	const size_t size = pool_block_size(p, v);
	memcpy(n, v, size);
	pool_free(p, v);
	return n;
}

#define BLK_COUNT (32) /* must be power of 2! */
#define BLK_SIZE  (32) /* must be power of 2! */

static bitmap_unit_t freelist_map[BLK_COUNT / sizeof(bitmap_unit_t)];
static uint64_t memory[BLK_COUNT*(BLK_SIZE  / sizeof(uint64_t))] = { 0 };

block_arena_t block_arena = {
	.freelist = {
		.bits = BLK_COUNT,
		.map  = freelist_map,
	},
	.blocksz = BLK_SIZE,
	.memory  = (void*)memory,
};

static uintptr_t diff(void *a, void *b) {
	return a > b ? (char*)a - (char*)b : (char*)b - (char*)a;
}

int block_test(void) {
	printf("block tests");
	printf("arena   = %p\n", (void*)&block_arena);
	printf("arena.m = %p\n", block_arena.memory);
	void *v1, *v2, *v3;
	v1 = block_arena_malloc_block(&block_arena, 12);
	assert(v1);
	printf("v1 = %p\n", v1);
	v2 = block_arena_malloc_block(&block_arena, 30);
	assert(v2);
	printf("v2 = %p\n", v2);
	assert(diff(v1, v2) == BLK_SIZE);
	block_arena_free_block(&block_arena, v1);
	v1 = block_arena_malloc_block(&block_arena, 12);
	printf("v1 = %p\n", v1);
	v3 = block_arena_malloc_block(&block_arena, 12);
	assert(v3);
	printf("v3 = %p\n", v3);
	assert(diff(v1, v3) == BLK_SIZE*2);

	block_arena_free_block(&block_arena, v1);
	block_arena_free_block(&block_arena, v2);
	block_arena_free_block(&block_arena, v3);
	size_t i = 0;
	for(i = 0; i < (BLK_COUNT+1); i++)
		if(!block_arena_malloc_block(&block_arena, 1))
			break;
	printf("exhausted at = %u\n", (unsigned)i);
	assert(i == BLK_COUNT);
	return 0;
}

