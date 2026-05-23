/* slab.c -- Slab allocator for small objects (32-1024 bytes).
 *
 * Pre-allocates 6 size classes (32, 64, 128, 256, 512, 1024 bytes).
 * Each class draws pages from vm_alloc_page() and splits them into
 * fixed-size slots with a magic-number header for free-list tracking.
 */

#include "freelib/slab.h"
#include "mm/vm.h"

#define SLAB_CLASSES 6
#define SLAB_MAGIC 0x534C4142u

typedef struct SlabNode {
    struct SlabNode* next;
} SlabNode;

typedef struct SlabHeader {
    uint32_t magic;
    uint32_t class_index;
} SlabHeader;

static const uint32_t class_sizes[SLAB_CLASSES] = { 32, 64, 128, 256, 512, 1024 };
static SlabNode* free_lists[SLAB_CLASSES];

void slab_init(void) {
    for (uint32_t i = 0; i < SLAB_CLASSES; i++)
        free_lists[i] = 0;
}


/* class_for -- determine the slab class index for a given allocation size.
 */static int class_for(size_t size) {
    size_t need = size + sizeof(SlabHeader);
    for (uint32_t i = 0; i < SLAB_CLASSES; i++) {
        if (need <= class_sizes[i])
            return (int)i;
    }
    return -1;
}


/* refill -- allocate a new page and split it into slots for a slab class.
 */static int refill(uint32_t ci) {
    uint8_t* page = (uint8_t*)vm_alloc_page();
    if (!page)
        return -1;
    uint32_t slot = class_sizes[ci];
    for (uint32_t off = 0; off + slot <= VM_PAGE_SIZE; off += slot) {
        SlabHeader* h = (SlabHeader*)(page + off);
        h->magic = SLAB_MAGIC;
        h->class_index = ci;
        SlabNode* n = (SlabNode*)(h + 1);
        n->next = free_lists[ci];
        free_lists[ci] = n;
    }
    return 0;
}

void* slab_alloc(size_t size) {
    int ci = class_for(size);
    if (ci < 0)
        return 0;
    if (!free_lists[ci] && refill((uint32_t)ci) != 0)
        return 0;
    SlabNode* n = free_lists[ci];
    free_lists[ci] = n->next;
    return n;
}

int slab_free(void* ptr) {
    if (!ptr)
        return 0;
    SlabHeader* h = ((SlabHeader*)ptr) - 1;
    if (h->magic != SLAB_MAGIC || h->class_index >= SLAB_CLASSES)
        return 0;
    SlabNode* n = (SlabNode*)ptr;
    n->next = free_lists[h->class_index];
    free_lists[h->class_index] = n;
    return 1;
}
