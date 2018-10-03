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
	/*size_t *used;    // memory used within block */
} block_arena_t;

void *block_arena_allocate_block(block_arena_t *a, size_t length);
void block_arena_free_block(block_arena_t *a, void *v);

block_arena_t *block_arena_allocate(size_t blocksz, size_t count);
void block_arena_free(block_arena_t *a);

extern block_arena_t block_arena;

#endif
