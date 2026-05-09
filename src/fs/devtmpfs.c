#include "fs/devtmpfs.h"
#include "drivers/entropy.h"
#include "drivers/input/ps2.h"

static int mounted;

static int streq(const char* a, const char* b) {
    uint32_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static int prefix(const char* path, const char* p) {
    uint32_t i = 0;
    while (p[i]) {
        if (path[i] != p[i]) return 0;
        i++;
    }
    return path[i] == '\0' || path[i] == '/';
}

static int copy_text(const char* s, uint32_t off, uint8_t* buf, uint32_t len, uint32_t* out) {
    uint32_t sl = 0;
    while (s[sl]) sl++;
    if (off >= sl) {
        *out = 0;
        return 0;
    }
    uint32_t take = sl - off;
    if (take > len) take = len;
    for (uint32_t i = 0; i < take; i++)
        buf[i] = (uint8_t)s[off + i];
    *out = take;
    return 0;
}

void devtmpfs_mount(void) {
    mounted = 1;
}

int devtmpfs_is_path(const char* path) {
    return mounted && path && prefix(path, "/dev");
}

int devtmpfs_pread(const char* path, uint32_t off, uint8_t* buf, uint32_t len, uint32_t* out) {
    if (!mounted || !path || !buf || !out)
        return -1;
    *out = 0;

    if (streq(path, "/dev")) {
        return copy_text("zero\nrandom\nurandom\nkbd\nmouse\nfstab\n", off, buf, len, out);
    }
    if (streq(path, "/dev/zero")) {
        for (uint32_t i = 0; i < len; i++) buf[i] = 0;
        *out = len;
        return 0;
    }
    if (streq(path, "/dev/random")) {
        for (uint32_t i = 0; i < len; i++) buf[i] = entropy_random_byte();
        *out = len;
        return 0;
    }
    if (streq(path, "/dev/urandom")) {
        for (uint32_t i = 0; i < len; i++) buf[i] = entropy_urandom_byte();
        *out = len;
        return 0;
    }
    if (streq(path, "/dev/kbd")) {
        uint32_t got = 0;
        while (got < len) {
            char c;
            if (!ps2_poll_char(&c))
                break;
            buf[got++] = (uint8_t)c;
            entropy_mix(((uint64_t)c << 32) ^ got);
        }
        *out = got;
        return 0;
    }
    if (streq(path, "/dev/mouse")) {
        uint32_t got = 0;
        while (got + 3 <= len) {
            uint8_t packet[3];
            if (!ps2_poll_mouse_packet(packet))
                break;
            buf[got++] = packet[0];
            buf[got++] = packet[1];
            buf[got++] = packet[2];
            entropy_mix(((uint64_t)packet[0] << 16) | ((uint64_t)packet[1] << 8) | packet[2]);
        }
        *out = got;
        return 0;
    }
    if (streq(path, "/dev/fstab")) {
        return copy_text("devtmpfs /dev devtmpfs rw 0 0\nproc /proc proc ro 0 0\nsysfs /sys sysfs ro 0 0\n", off, buf, len, out);
    }
    return -1;
}
