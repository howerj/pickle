#ifndef BLOCK_H
#define BLOCK_H

#include <stddef.h>
typedef unsigned bitmap_unit_t;
typedef struct {
	size_t bits;
	bitmap_unit_t *map;
} bitmap_t;

typedef struct {
	bitmap_t freelist; /* list of free blocks */
	size_t blocksz;    /* size of a block: 1, 2, 4, 8, ... */
	void *memory;      /* memory backing this allocator, should be aligned! */
} block_arena_t;

void *block_arena_malloc_block(block_arena_t *a, size_t length);
void *block_arena_calloc_block(block_arena_t *a, size_t length);
void block_arena_free_block(block_arena_t *a, void *v);
void *block_arena_realloc_block(block_arena_t *a, void *v, size_t length);

block_arena_t *block_arena_allocate(size_t blocksz, size_t count); /* count should be divisible by bitmap_unit_t */
void block_arena_free(block_arena_t *a);

int block_test(void);

#endif
