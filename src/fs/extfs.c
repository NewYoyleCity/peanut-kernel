/* extfs.c -- Extended Filesystem driver (ext2/3/4 read + write).
 *
 * Supports block-mapped (indirect) and extent-mapped inodes, directory
 * traversal, and file read/write.  Rejects 64-bit and metadata-checksum
 * features as unsupported.  Handles up to triply-indirect blocks.
 */

#include "fs/extfs.h"

#define EXT_SUPERBLOCK_OFFSET 1024u
#define EXT_SUPER_MAGIC 0xEF53u

#define EXT_INODE_MODE_DIR 0x4000u
#define EXT_INODE_MODE_REG 0x8000u

#define EXT_FEATURE_INCOMPAT_EXTENTS 0x40u
#define EXT_FEATURE_INCOMPAT_64BIT 0x80u
#define EXT_FEATURE_INCOMPAT_META_CSUM 0x10u

#define EXT4_EXTENTS_FL 0x80000u


/* le16 -- read little-endian 16-bit.
 */static uint16_t le16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

/* le32 -- read little-endian 32-bit.
 */static uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

/* le64 -- read little-endian 64-bit.
 */static uint64_t le64(const uint8_t* p) { return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32); }


/* streq -- exact string comparison.
 */static int streq(const char* a, const char* b) {
    uint32_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}


/* part_bytes_to_lba -- convert a byte offset to an absolute LBA.
 */static uint64_t part_bytes_to_lba(uint64_t first_lba, uint64_t byte_off) {
    return first_lba + (byte_off / BLOCK_SECTOR_SIZE);
}


/* read_bytes -- read an arbitrary number of bytes at a byte offset (handles sector boundaries).
 */static int read_bytes(BlockDevice* d, uint64_t first_lba, uint64_t off, void* out, uint32_t len) {
    uint8_t* dst = (uint8_t*)out;
    uint8_t sec[BLOCK_SECTOR_SIZE];
    uint64_t pos = off;
    uint32_t done = 0;
    while (done < len) {
        uint64_t lba = part_bytes_to_lba(first_lba, pos);
        uint32_t in = (uint32_t)(pos % BLOCK_SECTOR_SIZE);
        if (block_read(d, lba, 1, sec) != 0) return -1;
        uint32_t take = BLOCK_SECTOR_SIZE - in;
        if (take > (len - done)) take = len - done;
        for (uint32_t i = 0; i < take; i++) dst[done + i] = sec[in + i];
        done += take;
        pos += take;
    }
    return 0;
}


/* write_bytes -- write an arbitrary number of bytes at a byte offset.
 */static int write_bytes(BlockDevice* d, uint64_t first_lba, uint64_t off, const void* src_buf, uint32_t len) {
    const uint8_t* src = (const uint8_t*)src_buf;
    uint8_t sec[BLOCK_SECTOR_SIZE];
    uint64_t pos = off;
    uint32_t done = 0;
    while (done < len) {
        uint64_t lba = part_bytes_to_lba(first_lba, pos);
        uint32_t in = (uint32_t)(pos % BLOCK_SECTOR_SIZE);
        uint32_t take = BLOCK_SECTOR_SIZE - in;
        if (take > (len - done)) take = len - done;
        if (take != BLOCK_SECTOR_SIZE) {
            if (block_read(d, lba, 1, sec) != 0) return -1;
        }
        for (uint32_t i = 0; i < take; i++) sec[in + i] = src[done + i];
        if (block_write(d, lba, 1, sec) != 0) return -1;
        done += take;
        pos += take;
    }
    return 0;
}

int extfs_probe(BlockDevice* disk, uint64_t first_lba) {
    uint8_t sb[1024];
    if (!disk) return 0;
    if (read_bytes(disk, first_lba, EXT_SUPERBLOCK_OFFSET, sb, sizeof(sb)) != 0) return 0;
    if (le16(sb + 56) != EXT_SUPER_MAGIC) return 0;
    return 1;
}

int extfs_mount(ExtVolume* v, const Partition* p) {
    uint8_t sb[1024];
    if (!v || !p || !p->disk) return -1;
    if (read_bytes(p->disk, p->first_lba, EXT_SUPERBLOCK_OFFSET, sb, sizeof(sb)) != 0) return -1;
    if (le16(sb + 56) != EXT_SUPER_MAGIC) return -1;

    uint32_t log_block = le32(sb + 24);
    uint32_t block_size = 1024u << log_block;
    if (block_size < 1024u || block_size > 4096u) return -1;

    uint32_t blocks_per_group = le32(sb + 32);
    uint32_t inodes_per_group = le32(sb + 40);
    uint16_t inode_size = le16(sb + 88);
    if (inode_size < 128u || inode_size > 512u) return -1;

    uint32_t first_data_block = le32(sb + 20);
    uint32_t total_blocks = le32(sb + 4);
    if (blocks_per_group == 0 || inodes_per_group == 0 || total_blocks == 0) return -1;

    uint32_t feature_incompat = le32(sb + 96);
    if (feature_incompat & (EXT_FEATURE_INCOMPAT_64BIT | EXT_FEATURE_INCOMPAT_META_CSUM)) return -1;

    uint32_t groups = (total_blocks + blocks_per_group - 1u) / blocks_per_group;

    uint64_t gd_table_byte = (uint64_t)(first_data_block + 1u) * (uint64_t)block_size;
    v->partition = *p;
    v->block_size = block_size;
    v->blocks_per_group = blocks_per_group;
    v->inodes_per_group = inodes_per_group;
    v->inode_size = inode_size;
    v->first_data_block = first_data_block;
    v->groups = groups;
    v->group_desc_lba = (uint32_t)(p->first_lba + (gd_table_byte / BLOCK_SECTOR_SIZE));
    v->group_desc_off = (uint32_t)(gd_table_byte % BLOCK_SECTOR_SIZE);
    (void)feature_incompat;
    return 0;
}


/* read_group_desc -- read a 32-byte block group descriptor.
 */static int read_group_desc(ExtVolume* v, uint32_t group, uint8_t* out32) {
    uint64_t off = (uint64_t)v->group_desc_off + (uint64_t)group * 32ull;
    return read_bytes(v->partition.disk, v->partition.first_lba, (uint64_t)(v->group_desc_lba - v->partition.first_lba) * BLOCK_SECTOR_SIZE + off, out32, 32);
}


/* read_inode_raw -- read an inode's raw bytes by inode number.
 */static int read_inode_raw(ExtVolume* v, uint32_t inode, uint8_t* out, uint32_t out_len) {
    if (inode == 0) return -1;
    uint32_t idx = inode - 1u;
    uint32_t group = idx / v->inodes_per_group;
    uint32_t index = idx % v->inodes_per_group;
    if (group >= v->groups) return -1;

    uint8_t gd[32];
    if (read_group_desc(v, group, gd) != 0) return -1;
    uint32_t inode_table_block = le32(gd + 8);
    uint64_t inode_table_byte = (uint64_t)inode_table_block * (uint64_t)v->block_size;
    uint64_t inode_off = inode_table_byte + (uint64_t)index * (uint64_t)v->inode_size;
    uint32_t need = out_len < v->inode_size ? out_len : v->inode_size;
    return read_bytes(v->partition.disk, v->partition.first_lba, inode_off, out, need);
}


/* inode_mode -- extract file mode from inode.
 */static uint32_t inode_mode(const uint8_t* in) { return le16(in + 0); }

/* inode_flags -- extract inode flags.
 */static uint32_t inode_flags(const uint8_t* in) { return le32(in + 32); }

/* inode_size_bytes -- extract file size (64-bit) from inode.
 */static uint64_t inode_size_bytes(const uint8_t* in) {
    uint64_t lo = le32(in + 4);
    uint64_t hi = le32(in + 108);
    return lo | (hi << 32);
}


/* inode_block_ptr -- get direct block pointer by index.
 */static uint32_t inode_block_ptr(const uint8_t* in, uint32_t i) { return le32(in + 40 + i * 4u); }


/* read_u32_block -- read one uint32 from a block at a given index.
 */static uint32_t read_u32_block(ExtVolume* v, uint32_t block, uint32_t idx) {
    uint8_t sec[BLOCK_SECTOR_SIZE];
    uint32_t per_sec = BLOCK_SECTOR_SIZE / 4u;
    uint32_t which_sec = idx / per_sec;
    uint32_t off = (idx % per_sec) * 4u;
    uint64_t byte = (uint64_t)block * (uint64_t)v->block_size + (uint64_t)which_sec * BLOCK_SECTOR_SIZE;
    if (read_bytes(v->partition.disk, v->partition.first_lba, byte, sec, BLOCK_SECTOR_SIZE) != 0) return 0;
    return le32(sec + off);
}


/* map_indirect -- resolve a logical block number via indirect/triple-indirect blocks.
 */static uint32_t map_indirect(ExtVolume* v, const uint8_t* inode, uint32_t lbn) {
    uint32_t per = v->block_size / 4u;
    if (lbn < 12u) return inode_block_ptr(inode, lbn);
    lbn -= 12u;
    if (lbn < per) {
        uint32_t ib = inode_block_ptr(inode, 12);
        if (!ib) return 0;
        return read_u32_block(v, ib, lbn);
    }
    lbn -= per;
    if (lbn < per * per) {
        uint32_t dib = inode_block_ptr(inode, 13);
        if (!dib) return 0;
        uint32_t a = read_u32_block(v, dib, lbn / per);
        if (!a) return 0;
        return read_u32_block(v, a, lbn % per);
    }
    lbn -= per * per;
    uint32_t tib = inode_block_ptr(inode, 14);
    if (!tib) return 0;
    uint32_t a = read_u32_block(v, tib, lbn / (per * per));
    if (!a) return 0;
    uint32_t b = read_u32_block(v, a, (lbn / per) % per);
    if (!b) return 0;
    return read_u32_block(v, b, lbn % per);
}

struct ext4_extent_header { uint16_t magic, entries, max, depth; uint32_t gen; };
struct ext4_extent { uint32_t ee_block; uint16_t ee_len, ee_start_hi; uint32_t ee_start_lo; };
struct ext4_extent_idx { uint32_t ei_block; uint32_t ei_leaf_lo; uint16_t ei_leaf_hi; uint16_t unused; };


/* read_block_full -- read a complete filesystem block.
 */static int read_block_full(ExtVolume* v, uint32_t block, uint8_t* buf) {
    return read_bytes(v->partition.disk, v->partition.first_lba, (uint64_t)block * (uint64_t)v->block_size, buf, v->block_size);
}


/* map_extents_recurse -- resolve an LBN via the ext4 extent tree.
 */static uint32_t map_extents_recurse(ExtVolume* v, const uint8_t* node, uint32_t lbn) {
    const struct ext4_extent_header* h = (const struct ext4_extent_header*)node;
    if (h->magic != 0xF30A) return 0;
    if (h->depth == 0) {
        const struct ext4_extent* ex = (const struct ext4_extent*)(node + sizeof(*h));
        for (uint16_t i = 0; i < h->entries; i++) {
            uint32_t start = ex[i].ee_block;
            uint32_t len = ex[i].ee_len & 0x7FFFu;
            if (lbn < start || lbn >= start + len) continue;
            uint32_t p = ex[i].ee_start_lo;
            p |= (uint32_t)ex[i].ee_start_hi << 16;
            return p + (lbn - start);
        }
        return 0;
    }
    const struct ext4_extent_idx* ix = (const struct ext4_extent_idx*)(node + sizeof(*h));
    uint16_t pick = 0;
    for (uint16_t i = 0; i < h->entries; i++) {
        if (ix[i].ei_block <= lbn) pick = i;
    }
    uint32_t leaf = ix[pick].ei_leaf_lo | ((uint32_t)ix[pick].ei_leaf_hi << 16);
    static uint8_t buf[4096];
    if (v->block_size > sizeof(buf)) return 0;
    if (read_block_full(v, leaf, buf) != 0) return 0;
    return map_extents_recurse(v, buf, lbn);
}


/* map_block -- map LBN to physical block using extents or indirect.
 */static uint32_t map_block(ExtVolume* v, const uint8_t* inode, uint32_t lbn) {
    uint32_t flags = inode_flags(inode);
    if (flags & EXT4_EXTENTS_FL) {
        const uint8_t* root = inode + 40;
        return map_extents_recurse(v, root, lbn);
    }
    return map_indirect(v, inode, lbn);
}


/* read_dir_entries -- scan a directory for a named entry.
 */static int read_dir_entries(ExtVolume* v, uint32_t dir_inode, const char* name, uint32_t* out_inode, uint8_t want_dir) {
    uint8_t in[256];
    if (read_inode_raw(v, dir_inode, in, sizeof(in)) != 0) return -1;
    if ((inode_mode(in) & EXT_INODE_MODE_DIR) == 0) return -1;
    uint64_t size = inode_size_bytes(in);
    uint32_t blocks = (uint32_t)((size + v->block_size - 1u) / v->block_size);
    static uint8_t blk[4096];
    if (v->block_size > sizeof(blk)) return -1;
    for (uint32_t b = 0; b < blocks; b++) {
        uint32_t pb = map_block(v, in, b);
        if (!pb) return -1;
        if (read_block_full(v, pb, blk) != 0) return -1;
        uint32_t off = 0;
        while (off + 8u <= v->block_size) {
            uint32_t ino = le32(blk + off);
            uint16_t rec = le16(blk + off + 4);
            uint8_t nlen = blk[off + 6];
            uint8_t ftype = blk[off + 7];
            if (rec < 8u || off + rec > v->block_size) break;
            if (ino && nlen) {
                char tmp[256];
                uint32_t c = (nlen < sizeof(tmp) - 1) ? nlen : (uint32_t)sizeof(tmp) - 1;
                for (uint32_t i = 0; i < c; i++) tmp[i] = (char)blk[off + 8 + i];
                tmp[c] = '\0';
                if (streq(tmp, name)) {
                    if (want_dir) {
                        uint8_t child[256];
                        if (read_inode_raw(v, ino, child, sizeof(child)) != 0) return -1;
                        if ((inode_mode(child) & EXT_INODE_MODE_DIR) == 0) return -1;
                    }
                    (void)ftype;
                    *out_inode = ino;
                    return 0;
                }
            }
            off += rec;
        }
    }
    return -1;
}

int extfs_find_root(ExtVolume* v, const char* path, uint32_t* out_inode) {
    if (!v || !path || !out_inode) return -1;
    uint32_t cur = 2;
    uint32_t i = 0;
    if (path[0] == '/') i = 1;
    while (path[i]) {
        char comp[256];
        uint32_t cs = 0;
        while (path[i] && path[i] != '/') {
            if (cs + 1 >= sizeof(comp)) return -1;
            comp[cs++] = path[i++];
        }
        comp[cs] = '\0';
        while (path[i] == '/') i++;
        uint8_t last = path[i] == '\0';
        uint32_t next;
        if (read_dir_entries(v, cur, comp, &next, last ? 0 : 1) != 0) return -1;
        cur = next;
    }
    *out_inode = cur;
    return 0;
}

int extfs_stat(ExtVolume* v, const char* path, uint64_t* size, int* is_dir) {
    uint32_t ino;
    uint8_t in[256];
    if (!v || !path || !size || !is_dir) return -1;
    if (extfs_find_root(v, path, &ino) != 0) return -1;
    if (read_inode_raw(v, ino, in, sizeof(in)) != 0) return -1;
    *size = inode_size_bytes(in);
    *is_dir = (inode_mode(in) & EXT_INODE_MODE_DIR) != 0;
    return 0;
}

int extfs_read_file(ExtVolume* v, const char* path, void* buffer, uint32_t buffer_size, uint32_t* bytes_read) {
    uint32_t ino;
    if (!v || !path || !buffer || !bytes_read) return -1;
    *bytes_read = 0;
    if (extfs_find_root(v, path, &ino) != 0) return -1;
    uint8_t in[256];
    if (read_inode_raw(v, ino, in, sizeof(in)) != 0) return -1;
    if ((inode_mode(in) & EXT_INODE_MODE_REG) == 0) return -1;
    uint64_t size = inode_size_bytes(in);
    if (size > buffer_size) return -1;
    uint32_t blocks = (uint32_t)((size + v->block_size - 1u) / v->block_size);
    static uint8_t blk[4096];
    if (v->block_size > sizeof(blk)) return -1;
    uint64_t copied = 0;
    for (uint32_t b = 0; b < blocks; b++) {
        uint32_t pb = map_block(v, in, b);
        if (!pb) return -1;
        if (read_block_full(v, pb, blk) != 0) return -1;
        uint32_t take = v->block_size;
        if (copied + take > size) take = (uint32_t)(size - copied);
        for (uint32_t j = 0; j < take; j++) ((uint8_t*)buffer)[copied + j] = blk[j];
        copied += take;
    }
    *bytes_read = (uint32_t)copied;
    return 0;
}

int extfs_write_file(ExtVolume* v, const char* path, const void* buffer, uint32_t buffer_size) {
    uint32_t ino;
    if (!v || !path || !buffer) return -1;
    if (extfs_find_root(v, path, &ino) != 0) return -1;

    uint8_t in[256];
    if (read_inode_raw(v, ino, in, sizeof(in)) != 0) return -1;
    if ((inode_mode(in) & EXT_INODE_MODE_REG) == 0) return -1;

    uint64_t size = inode_size_bytes(in);
    if (buffer_size > size) return -1;
    if (size == 0) return (buffer_size == 0) ? 0 : -1;

    uint32_t blocks = (uint32_t)((size + v->block_size - 1u) / v->block_size);
    static uint8_t blk[4096];
    if (v->block_size > sizeof(blk)) return -1;

    uint64_t written = 0;
    for (uint32_t b = 0; b < blocks; b++) {
        uint32_t pb = map_block(v, in, b);
        if (!pb) return -1;

        uint32_t take = v->block_size;
        if (written + (uint64_t)take > size) take = (uint32_t)(size - written);

        if (read_block_full(v, pb, blk) != 0) return -1;
        for (uint32_t j = 0; j < take; j++) {
            uint64_t src_off = written + j;
            if (src_off >= buffer_size) break;
            blk[j] = ((const uint8_t*)buffer)[src_off];
        }

        if (write_bytes(v->partition.disk, v->partition.first_lba,
                        (uint64_t)pb * (uint64_t)v->block_size,
                        blk, v->block_size) != 0)
            return -1;

        written += (uint64_t)take;
        if (written >= (uint64_t)buffer_size) break;
    }

    return written == (uint64_t)buffer_size ? 0 : -1;
}

int extfs_dir_exists(ExtVolume* v, const char* path) {
    uint32_t ino;
    uint8_t in[256];
    if (!v || !path) return 0;
    if (extfs_find_root(v, path, &ino) != 0) return 0;
    if (read_inode_raw(v, ino, in, sizeof(in)) != 0) return 0;
    return (inode_mode(in) & EXT_INODE_MODE_DIR) != 0;
}
