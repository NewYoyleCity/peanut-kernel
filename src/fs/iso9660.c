#include "iso9660.h"
#include "freelib/kstdio.h"
#include "freelib/kalloc.h"
#include "drivers/block/cdrom.h"

static iso9660_pvd_t *iso_pvd = NULL;
static uint32_t iso_root_lba = 0;
static uint32_t iso_root_size = 0;

static uint16_t iso_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t iso_le32(const uint8_t *p) {
    return (uint32_t)p[0] |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}

static char iso_upper(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - ('a' - 'A'));
    return c;
}

static int iso_component_equal(const char *path, uint32_t path_len, const uint8_t *name, uint8_t name_len) {
    uint32_t effective_len = name_len;

    while (effective_len > 0 && name[effective_len - 1u] == ' ') effective_len--;
    if (effective_len > 2 && name[effective_len - 2u] == ';' && name[effective_len - 1u] == '1') {
        effective_len -= 2;
    }
    if (effective_len > 0 && name[effective_len - 1u] == '.') effective_len--;
    if (path_len != effective_len) return 0;

    for (uint32_t i = 0; i < path_len; i++) {
        if (iso_upper(path[i]) != iso_upper((char)name[i])) return 0;
    }
    return 1;
}

static void iso_next_component(const char **path, const char **name, uint32_t *len) {
    const char *p = *path;
    while (*p == '/') p++;
    *name = p;
    *len = 0;
    while (p[*len] && p[*len] != '/') (*len)++;
    *path = p + *len;
}

static int iso_find_in_dir(uint32_t dir_lba, uint32_t dir_size, const char *path,
    uint32_t *out_lba, uint32_t *out_size, int *out_is_dir) {
    uint8_t sector[ISO9660_SECTOR_SIZE];
    const char *component;
    uint32_t component_len;

    iso_next_component(&path, &component, &component_len);
    if (component_len == 0) {
        *out_lba = dir_lba;
        *out_size = dir_size;
        *out_is_dir = 1;
        return 0;
    }

    for (uint32_t pos = 0; pos < dir_size; ) {
        uint32_t sector_index = pos / ISO9660_SECTOR_SIZE;
        uint32_t sector_off = pos % ISO9660_SECTOR_SIZE;
        if (cdrom_read(0, dir_lba + sector_index, sector, 1) != 0) return -1;

        while (sector_off < ISO9660_SECTOR_SIZE && pos < dir_size) {
            uint8_t len = sector[sector_off];
            if (len == 0) {
                pos = (sector_index + 1u) * ISO9660_SECTOR_SIZE;
                break;
            }
            if (sector_off + len > ISO9660_SECTOR_SIZE || len < 34) return -1;

            uint8_t *record = sector + sector_off;
            uint8_t name_len = record[32];
            uint8_t *name = record + 33;
            uint32_t extent_lba = iso_le32(record + 2);
            uint32_t extent_size = iso_le32(record + 10);
            int is_dir = (record[25] & 0x02) ? 1 : 0;

            if (name_len > 1 || (name[0] != 0 && name[0] != 1)) {
                if (iso_component_equal(component, component_len, name, name_len)) {
                    while (*path == '/') path++;
                    if (*path == '\0') {
                        *out_lba = extent_lba;
                        *out_size = extent_size;
                        *out_is_dir = is_dir;
                        return 0;
                    }
                    if (!is_dir) return -1;
                    return iso_find_in_dir(extent_lba, extent_size, path, out_lba, out_size, out_is_dir);
                }
            }

            sector_off += len;
            pos += len;
        }
    }

    return -1;
}

int iso9660_init(void) {
    kprint("Initializing ISO9660 filesystem...\n");
    
    /* ISO9660 stores the Primary Volume Descriptor at logical sector 16. */
    uint8_t *sector_buffer = kalloc(ISO9660_SECTOR_SIZE);
    if (!sector_buffer) {
        return -1;
    }
    
    int ret = cdrom_read(0, 16, sector_buffer, 1);
    if (ret != 0) {
        kfree(sector_buffer);
        return -1;
    }
    
    iso_pvd = (iso9660_pvd_t *)sector_buffer;
    
    if (sector_buffer[0] != 1 ||
        sector_buffer[1] != 'C' ||
        sector_buffer[2] != 'D' ||
        sector_buffer[3] != '0' ||
        sector_buffer[4] != '0' ||
        sector_buffer[5] != '1') {
        kfree(sector_buffer);
        return -1;
    }

    iso_root_lba = iso_le32(sector_buffer + 156 + 2);
    iso_root_size = iso_le32(sector_buffer + 156 + 10);
    
    kprint("ISO9660 filesystem initialized\n");
    return 0;
}

int iso9660_read(const char *path, uint8_t *buffer, uint32_t len) {
    uint32_t lba;
    uint32_t size;
    int is_dir;
    uint32_t copied = 0;
    uint8_t sector[ISO9660_SECTOR_SIZE];

    if (!iso_pvd || !path || !buffer) return -1;
    if (iso_find_in_dir(iso_root_lba, iso_root_size, path, &lba, &size, &is_dir) != 0) return -1;
    if (is_dir || len < size) return -1;

    while (copied < size) {
        if (cdrom_read(0, lba + copied / ISO9660_SECTOR_SIZE, sector, 1) != 0) return -1;
        uint32_t left = size - copied;
        uint32_t count = left < ISO9660_SECTOR_SIZE ? left : ISO9660_SECTOR_SIZE;
        for (uint32_t i = 0; i < count; i++) buffer[copied + i] = sector[i];
        copied += count;
    }

    return (int)copied;
}

int iso9660_stat(const char *path, uint32_t *size, int *is_dir) {
    uint32_t lba;
    uint32_t found_size;
    int found_is_dir;

    if (!iso_pvd || !path) return -1;
    if (iso_find_in_dir(iso_root_lba, iso_root_size, path, &lba, &found_size, &found_is_dir) != 0) return -1;
    (void)lba;
    if (size) *size = found_size;
    if (is_dir) *is_dir = found_is_dir;
    return 0;
}
