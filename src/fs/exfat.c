#include "fs/exfat.h"

#define EXFAT_EOC 0xFFFFFFFFu

#define EXFAT_ENTRY_FILE 0x85
#define EXFAT_ENTRY_STREAM 0xC0
#define EXFAT_ENTRY_FILENAME 0xC1

#define EXFAT_MAX_CLUSTER_SECTORS 8

static uint16_t le16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}

static uint64_t le64(const uint8_t* p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}

static int streq_ci(const char* a, const char* b) {
    uint32_t i = 0;
    for (;;) {
        char x = a[i];
        char y = b[i];
        if (x >= 'a' && x <= 'z')
            x = (char)(x - 32);
        if (y >= 'a' && y <= 'z')
            y = (char)(y - 32);
        if (x != y)
            return 0;
        if (x == '\0')
            return 1;
        i++;
    }
}

int exfat_probe_boot(BlockDevice* disk, uint64_t first_lba) {
    uint8_t s[BLOCK_SECTOR_SIZE];
    if (!disk)
        return 0;
    if (block_read(disk, first_lba, 1, s) != 0)
        return 0;
    if (s[510] != 0x55 || s[511] != 0xAA)
        return 0;
    static const char sig[8] = { 'E', 'X', 'F', 'A', 'T', ' ', ' ', ' ' };
    for (int i = 0; i < 8; i++) {
        if (s[3 + i] != (uint8_t)sig[i])
            return 0;
    }
    return 1;
}

static uint32_t cluster_first_lba(ExfatVolume* v, uint32_t cluster) {
    if (cluster < 2 || cluster > v->cluster_count)
        return 0;
    return v->cluster_heap_lba + (cluster - 2u) * (uint32_t)v->sectors_per_cluster;
}

static int read_cluster(ExfatVolume* v, uint32_t cluster, uint8_t* buffer) {
    uint32_t lba = cluster_first_lba(v, cluster);
    if (lba == 0)
        return -1;
    return block_read(v->partition.disk, lba, v->sectors_per_cluster, buffer);
}

static uint32_t next_cluster(ExfatVolume* v, uint32_t cluster) {
    uint64_t fat_byte = (uint64_t)cluster * 4ull;
    uint64_t sec = fat_byte / BLOCK_SECTOR_SIZE;
    unsigned off = (unsigned)(fat_byte % BLOCK_SECTOR_SIZE);
    uint8_t s[BLOCK_SECTOR_SIZE];
    if (block_read(v->partition.disk, v->fat_start_lba + sec, 1, s) != 0)
        return EXFAT_EOC;
    uint32_t val = le32(s + off);
    if (val >= 0xFFFFFFF8u)
        return EXFAT_EOC;
    return val;
}

int exfat_mount(ExfatVolume* volume, const Partition* partition) {
    uint8_t s[BLOCK_SECTOR_SIZE];
    if (!volume || !partition || !partition->disk)
        return -1;
    if (!exfat_probe_boot(partition->disk, partition->first_lba))
        return -1;
    if (block_read(partition->disk, partition->first_lba, 1, s) != 0)
        return -1;

    uint8_t bps_shift = s[108];
    uint8_t spc_shift = s[109];
    if (bps_shift != 9)
        return -1;

    uint32_t fat_off = le32(s + 80);
    uint32_t fat_len = le32(s + 84);
    uint32_t heap_off = le32(s + 88);
    uint32_t cluster_count = le32(s + 92);
    uint32_t root_cluster = le32(s + 96);
    uint8_t num_fats = s[110];
    if (spc_shift > 7 || num_fats < 1 || fat_len == 0 || cluster_count < 2 || root_cluster < 2)
        return -1;

    uint8_t spc = (uint8_t)(1u << spc_shift);
    if (spc > EXFAT_MAX_CLUSTER_SECTORS)
        return -1;

    volume->partition = *partition;
    volume->fat_start_lba = (uint32_t)(partition->first_lba + fat_off);
    volume->cluster_heap_lba = (uint32_t)(partition->first_lba + heap_off);
    volume->root_cluster = root_cluster;
    volume->cluster_count = cluster_count;
    volume->sectors_per_cluster = spc;
    (void)fat_len;
    return 0;
}

static int parse_entry_set(const uint8_t* buf, uint32_t bytes, uint32_t off, ExfatDirEntry* out) {
    if (off + 32 > bytes)
        return -1;
    if (buf[off] != EXFAT_ENTRY_FILE)
        return -1;

    uint8_t secondary = buf[off + 1];
    if (secondary < 1 || off + (uint32_t)(secondary + 1u) * 32u > bytes)
        return -1;

    uint16_t attrs = le16(buf + off + 4);
    const uint8_t* stream = buf + off + 32;
    if (stream[0] != EXFAT_ENTRY_STREAM)
        return -1;

    uint8_t name_len = stream[3];
    uint32_t first_cluster = le32(stream + 20);
    uint64_t data_length = le64(stream + 24);

    uint16_t name_u[EXFAT_MAX_NAME];
    uint32_t name_pos = 0;
    for (uint8_t si = 2; si <= secondary && name_pos < name_len && name_pos < EXFAT_MAX_NAME; si++) {
        const uint8_t* e = buf + off + 32u * (uint32_t)si;
        if (e + 32 > buf + bytes)
            break;
        if (e[0] != EXFAT_ENTRY_FILENAME)
            continue;
        for (int k = 0; k < 15 && name_pos < name_len && name_pos < EXFAT_MAX_NAME - 1; k++) {
            if (2 + (k + 1) * 2 > 32)
                break;
            name_u[name_pos++] = le16(e + 2 + (uint32_t)k * 2u);
        }
    }

    uint32_t copy_len = name_pos < EXFAT_MAX_NAME - 1 ? name_pos : EXFAT_MAX_NAME - 1;
    for (uint32_t i = 0; i < copy_len; i++) {
        uint16_t c = name_u[i];
        out->name[i] = c < 128 ? (char)c : '?';
    }
    out->name[copy_len] = '\0';

    out->attributes = (uint8_t)(attrs & 0xFF);
    out->first_cluster = first_cluster;
    out->size = data_length;
    return 0;
}

static uint32_t list_cluster(ExfatVolume* v, uint32_t cluster, ExfatDirEntry* entries, uint32_t max_entries) {
    uint8_t buf[BLOCK_SECTOR_SIZE * EXFAT_MAX_CLUSTER_SECTORS];
    uint32_t count = 0;
    uint32_t bytes = (uint32_t)v->sectors_per_cluster * BLOCK_SECTOR_SIZE;

    while (cluster >= 2 && cluster < EXFAT_EOC && count < max_entries) {
        if (read_cluster(v, cluster, buf) != 0)
            break;
        for (uint32_t off = 0; off < bytes && count < max_entries; ) {
            if (buf[off] == 0)
                return count;
            if (buf[off] == 0x80 || buf[off] == 0x81) {
                off += 32;
                continue;
            }
            if (buf[off] != EXFAT_ENTRY_FILE) {
                off += 32;
                continue;
            }
            uint8_t sec = buf[off + 1];
            ExfatDirEntry ent;
            if (parse_entry_set(buf, bytes, off, &ent) != 0) {
                off += 32;
                continue;
            }
            entries[count++] = ent;
            off += 32u * (uint32_t)(sec + 1u);
        }
        cluster = next_cluster(v, cluster);
    }
    return count;
}

static int find_in_cluster_chain(ExfatVolume* v, uint32_t cluster, const char* name, ExfatDirEntry* out) {
    ExfatDirEntry entries[64];
    uint32_t n = list_cluster(v, cluster, entries, 64);
    for (uint32_t i = 0; i < n; i++) {
        if (streq_ci(entries[i].name, name)) {
            *out = entries[i];
            return 0;
        }
    }
    return -1;
}

int exfat_dir_exists(ExfatVolume* volume, const char* dirname) {
    ExfatDirEntry e;
    if (exfat_find_root(volume, dirname, &e) != 0)
        return 0;
    return (e.attributes & EXFAT_ATTR_DIRECTORY) != 0;
}

int exfat_find_root(ExfatVolume* volume, const char* path, ExfatDirEntry* out) {
    if (!volume || !path || !out)
        return -1;

    const char* p = path;
    uint32_t dir_cluster = volume->root_cluster;
    ExfatDirEntry current;

    while (*p == '/' || *p == '\\')
        p++;

    while (*p) {
        char component[EXFAT_MAX_NAME];
        uint32_t len = 0;

        while (p[len] && p[len] != '/' && p[len] != '\\') {
            if (len + 1 >= sizeof(component))
                return -1;
            component[len] = p[len];
            len++;
        }
        component[len] = '\0';
        while (p[len] == '/' || p[len] == '\\')
            len++;

        if (find_in_cluster_chain(volume, dir_cluster, component, &current) != 0)
            return -1;

        p += len;
        if (!*p) {
            *out = current;
            return 0;
        }
        if ((current.attributes & EXFAT_ATTR_DIRECTORY) == 0)
            return -1;
        dir_cluster = current.first_cluster;
    }

    return -1;
}

int exfat_read_file(ExfatVolume* volume, const char* name, void* buffer, uint32_t buffer_size, uint32_t* bytes_read) {
    ExfatDirEntry file;
    uint8_t cluster_buf[BLOCK_SECTOR_SIZE * EXFAT_MAX_CLUSTER_SECTORS];
    uint8_t* dst = (uint8_t*)buffer;
    uint64_t copied = 0;

    if (!volume || !name || !buffer || !bytes_read)
        return -1;
    if (exfat_find_root(volume, name, &file) != 0)
        return -1;
    if (file.attributes & EXFAT_ATTR_DIRECTORY)
        return -1;
    if (file.size > buffer_size)
        return -1;

    uint32_t cluster = file.first_cluster;
    while (cluster >= 2 && cluster < EXFAT_EOC && copied < file.size) {
        if (read_cluster(volume, cluster, cluster_buf) != 0)
            return -1;
        uint32_t chunk = (uint32_t)volume->sectors_per_cluster * BLOCK_SECTOR_SIZE;
        uint64_t left = file.size - copied;
        uint32_t to_copy = left < chunk ? (uint32_t)left : chunk;
        for (uint32_t i = 0; i < to_copy; i++)
            dst[copied + i] = cluster_buf[i];
        copied += to_copy;
        cluster = next_cluster(volume, cluster);
    }

    *bytes_read = (uint32_t)copied;
    return copied == file.size ? 0 : -1;
}

int exfat_write_file(ExfatVolume* volume, const char* name, const void* buffer, uint32_t buffer_size) {
    ExfatDirEntry file;
    uint8_t cluster_buf[BLOCK_SECTOR_SIZE * EXFAT_MAX_CLUSTER_SECTORS];

    if (!volume || !name || !buffer)
        return -1;
    if (exfat_find_root(volume, name, &file) != 0)
        return -1;
    if (file.attributes & EXFAT_ATTR_DIRECTORY)
        return -1;

    if (buffer_size > file.size)
        return -1;

    uint32_t cluster = file.first_cluster;
    uint64_t written = 0;

    uint32_t chunk = (uint32_t)volume->sectors_per_cluster * BLOCK_SECTOR_SIZE;
    const uint8_t* src = (const uint8_t*)buffer;

    while (cluster >= 2 && cluster < EXFAT_EOC && written < buffer_size) {
        if (read_cluster(volume, cluster, cluster_buf) != 0)
            return -1;

        uint64_t left = (uint64_t)buffer_size - written;
        uint32_t to_copy = (uint32_t)(left < chunk ? left : chunk);

        for (uint32_t i = 0; i < to_copy; i++)
            cluster_buf[i] = src[written + i];

        uint32_t lba = cluster_first_lba(volume, cluster);
        if (lba == 0)
            return -1;

        if (block_write(volume->partition.disk, lba, volume->sectors_per_cluster, cluster_buf) != 0)
            return -1;

        written += to_copy;
        cluster = next_cluster(volume, cluster);
    }

    return written == (uint64_t)buffer_size ? 0 : -1;
}
