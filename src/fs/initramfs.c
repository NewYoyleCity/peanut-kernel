#include "fs/initramfs.h"
#include "freelib/kstdio.h"

extern uint8_t _initramfs_start[];
extern uint8_t _initramfs_end[];

static uint32_t kslen(const char* s) {
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

static int cpio_is_newc(const uint8_t* p, uint32_t remain) {
    if (remain < 6)
        return 0;
    return p[0] == '0' && p[1] == '7' && p[2] == '0' && p[3] == '7' && p[4] == '0' && p[5] == '1';
}

static uint32_t cpio_hex(const uint8_t* f, uint32_t n) {
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t c = f[i];
        uint32_t d;
        if (c >= '0' && c <= '9')
            d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f')
            d = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            d = (uint32_t)(c - 'A' + 10);
        else
            return 0;
        v = (v << 4) | d;
    }
    return v;
}

static int name_matches_exec_path(const char* want, const char* cpio_name, uint32_t namesz) {
    if (namesz < 2)
        return 0;
    if (cpio_name[namesz - 1] != '\0')
        return 0;
    const char* w = want;
    if (w[0] == '/')
        w++;
    uint32_t wl = kslen(w);
    uint32_t nl = namesz - 1;
    if (wl != nl)
        return 0;
    for (uint32_t i = 0; i < wl; i++) {
        char a = w[i];
        char b = cpio_name[i];
        if (a >= 'a' && a <= 'z')
            a = (char)(a - 32);
        if (b >= 'a' && b <= 'z')
            b = (char)(b - 32);
        if (a != b)
            return 0;
    }
    return 1;
}

static int is_trailer(const char* name, uint32_t namesz) {
    const char* t = "TRAILER!!!";
    uint32_t tl = kslen(t) + 1;
    if (namesz != tl)
        return 0;
    for (uint32_t i = 0; i < tl - 1; i++) {
        if (name[i] != t[i])
            return 0;
    }
    return name[tl - 1] == '\0';
}

void initramfs_init(void) {
    uintptr_t start = (uintptr_t)_initramfs_start;
    uintptr_t end = (uintptr_t)_initramfs_end;
    if (end > start) {
        kprint("initramfs: embedded archive found (");
        kprint_int((int32_t)(end - start));
        kprint(" bytes)\n");
    } else {
        kprint("initramfs: no embedded archive\n");
    }
}

int initramfs_find(const char* path, const uint8_t** data, uint32_t* size) {
    uintptr_t start = (uintptr_t)_initramfs_start;
    uintptr_t end = (uintptr_t)_initramfs_end;
    if (!path || end <= start) {
        if (data) *data = NULL;
        if (size) *size = 0;
        return -1;
    }

    uint32_t off = 0;
    const uint8_t* base = (const uint8_t*)start;
    uint32_t total = (uint32_t)(end - start);

    while (off + 110 <= total) {
        const uint8_t* h = base + off;
        if (!cpio_is_newc(h, total - off))
            break;
        uint32_t namesz = cpio_hex(h + 54, 8);
        uint32_t filesz = cpio_hex(h + 54 + 8, 8);
        if (namesz == 0 && filesz == 0)
            break;
        uint32_t head = 110;
        uint32_t namepad = (namesz + 3) & ~3u;
        uint32_t filepad = (filesz + 3) & ~3u;
        if (off + head + namepad + filepad > total)
            break;
        const char* name = (const char*)(h + head);
        if (is_trailer(name, namesz))
            break;
        if (name_matches_exec_path(path, name, namesz)) {
            const uint8_t* filedata = h + head + namepad;
            if (data) *data = filedata;
            if (size) *size = filesz;
            return 0;
        }
        off += head + namepad + filepad;
    }
    if (data) *data = NULL;
    if (size) *size = 0;
    return -1;
}
