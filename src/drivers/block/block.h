#ifndef BLOCK_H
#define BLOCK_H

#include "freelib/kstdint.h"

#define BLOCK_SECTOR_SIZE 512

typedef struct BlockDevice BlockDevice;

typedef int (*BlockReadFn)(BlockDevice* dev, uint64_t lba, uint32_t count, void* buffer);
typedef int (*BlockWriteFn)(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buffer);

struct BlockDevice {
    const char* name;
    uint64_t sector_count;
    uint32_t sector_size;
    void* driver_data;
    BlockReadFn read;
    BlockWriteFn write;
};

int block_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* buffer);
int block_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buffer);

#endif
