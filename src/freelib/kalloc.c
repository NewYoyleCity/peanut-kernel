/* kalloc.c -- Heap allocator (fallback for large allocations).
 *
 * A simple bump+free-list allocator.  Small allocations (<=1024 bytes)
 * are first tried by the slab allocator; large allocations use block
 * headers embedded in the heap region after _kernel_end.
 */

#include "kalloc.h"
#include "freelib/slab.h"

extern uint8_t _kernel_end;

typedef struct BlockHeader {
    size_t size;
    int is_free;
    struct BlockHeader* next;
} BlockHeader;

static BlockHeader* heap_start = NULL;


/* kalloc_init -- initialise the heap allocator (sets the first block header at _kernel_end).
 */void kalloc_init() {
    if (heap_start) return;

    uintptr_t heap_addr = ((uintptr_t)&_kernel_end + 15) & ~((uintptr_t)15);
    heap_start = (BlockHeader*)heap_addr;
    heap_start->size = 0;
    heap_start->is_free = 1;
    heap_start->next = NULL;
}

void* kalloc(size_t size) {
    kalloc_init();

    if (size <= 1024) {
        void* small = slab_alloc(size);
        if (small)
            return small;
    }

    size = (size + 15) & ~15;

    BlockHeader* current = heap_start;

    while (current->size != 0) {
        if (current->is_free && current->size >= size) {
            current->is_free = 0;
            return (void*)(current + 1);
        }
        if (!current->next) break;
        current = current->next;
    }

    uintptr_t new_block_addr;
    if (current->size == 0) {
        new_block_addr = (uintptr_t)heap_start;
    } else {
        new_block_addr = (uintptr_t)current + sizeof(BlockHeader) + current->size;
    }

    BlockHeader* new_block = (BlockHeader*)new_block_addr;
    new_block->size = size;
    new_block->is_free = 0;
    new_block->next = NULL;

    if (current != new_block) {
        current->next = new_block;
    }

    return (void*)(new_block + 1);
}

void kfree(void* ptr) {
    if (!ptr) return;

    if (slab_free(ptr))
        return;
    kfree_large(ptr);
}

void kfree_large(void* ptr) {
    if (!ptr) return;

    BlockHeader* header = (BlockHeader*)ptr - 1;
    header->is_free = 1;
}
