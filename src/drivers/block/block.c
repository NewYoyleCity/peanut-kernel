/* block.c -- Block device abstraction layer.
 *
 * Provides thread-safe wrappers (using a global spinlock) around
 * the read/write function pointers of BlockDevice instances.
 */

#include "drivers/block/block.h"
#include "cpu/spinlock.h"

static spinlock_t block_lock = { .v = 0 };

int block_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* buffer) {
    if (!dev || !dev->read || !buffer || count == 0) return -1;
    if (dev->sector_size != BLOCK_SECTOR_SIZE) return -1;
    spin_lock(&block_lock);
    int r = dev->read(dev, lba, count, buffer);
    spin_unlock(&block_lock);
    return r;
}

int block_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buffer) {
    if (!dev || !dev->write || !buffer || count == 0) return -1;
    if (dev->sector_size != BLOCK_SECTOR_SIZE) return -1;
    spin_lock(&block_lock);
    int r = dev->write(dev, lba, count, buffer);
    spin_unlock(&block_lock);
    return r;
}
