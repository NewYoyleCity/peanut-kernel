/* devtmpfs.c -- Device temporary filesystem mounted at /dev.
 *
 * Exposes kernel devices (/dev/zero, /dev/random, /dev/urandom,
 * /dev/kbd, /dev/mouse, /dev/ttyS0, /dev/fstab) as pseudo-files.
 */

#include "fs/devtmpfs.h"
#include "drivers/entropy.h"
#include "drivers/input/ps2.h"
#include "config.h"

#ifdef CONFIG_TTY
#include "drivers/char/tty.h"
#endif

#ifdef CONFIG_USB_XHCI
int usb_kbd_poll_char(char* out);
int usb_mouse_poll_packet(uint8_t packet[4]);
#endif


static int mounted;


/* streq -- compare two strings for exact equality.
 */static int streq(const char* a, const char* b) {
    uint32_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}


/* prefix -- check if path has the given prefix at component boundary.
 */static int prefix(const char* path, const char* p) {
    uint32_t i = 0;
    while (p[i]) {
        if (path[i] != p[i]) return 0;
        i++;
    }
    return path[i] == '\0' || path[i] == '/';
}


/* copy_text -- extract a substring from a static text buffer.
 */static int copy_text(const char* s, uint32_t off, uint8_t* buf, uint32_t len, uint32_t* out) {
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


/* devtmpfs_mount -- mark the devtmpfs as mounted.
 */
void devtmpfs_mount(void) {
    mounted = 1;
}


/* devtmpfs_is_path -- check if a path lives under /dev.
 */
int devtmpfs_is_path(const char* path) {
    return mounted && path && prefix(path, "/dev");
}


/* devtmpfs_pread -- read from a /dev pseudo-file (zero, random, kbd, mouse, tty, etc.).
 */
int devtmpfs_pread(const char* path, uint32_t off, uint8_t* buf, uint32_t len, uint32_t* out) {
    if (!mounted || !path || !buf || !out)
        return -1;
    *out = 0;

    if (streq(path, "/dev")) {
        return copy_text("zero\nrandom\nurandom\nkbd\nmouse\nttyS0\nfstab\n", off, buf, len, out);
    }
#ifdef CONFIG_TTY
    if (streq(path, "/dev/ttyS0")) {
        uint32_t got = 0;
        while (got < len && tty_can_read()) {
            char c;
            if (tty_read_char(&c) != 0) break;
            buf[got++] = (uint8_t)c;
        }
        *out = got;
        return 0;
    }
#endif
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
            char c = 0;
            int have = ps2_poll_char(&c);
#ifdef CONFIG_USB_XHCI
            if (!have)
                have = (usb_kbd_poll_char(&c) == 0) ? 1 : 0;
#endif
            if (!have)
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
            uint8_t packet[4];
            int have = ps2_poll_mouse_packet(packet);
#ifdef CONFIG_USB_XHCI
            if (!have)
                have = (usb_mouse_poll_packet(packet) == 0) ? 1 : 0;
#endif
            if (!have)
                break;
            buf[got++] = packet[0];
            buf[got++] = packet[1];
            buf[got++] = packet[2];
            entropy_mix(((uint64_t)packet[0] << 16) | ((uint64_t)packet[1] << 8) | packet[2]);
            if (got + 3 <= len && packet[3]) {
                buf[got++] = packet[3];
            }
        }
        *out = got;
        return 0;
    }
    if (streq(path, "/dev/fstab")) {
        return copy_text("devtmpfs /dev devtmpfs rw 0 0\nproc /proc proc ro 0 0\nsysfs /sys sysfs ro 0 0\n", off, buf, len, out);
    }
    return -1;
}


/* devtmpfs_pwrite -- write to a /dev pseudo-file (currently only /dev/ttyS0).
 */
int devtmpfs_pwrite(const char* path, uint32_t off, const uint8_t* buf, uint32_t len, uint32_t* out) {
    (void)off;
    if (!mounted || !path || !buf || !out)
        return -1;
    *out = 0;
#ifdef CONFIG_TTY
    if (streq(path, "/dev/ttyS0")) {
        for (uint32_t i = 0; i < len; i++) {
            if (tty_write_char((char)buf[i]) != 0) break;
            (*out)++;
        }
        return 0;
    }
#endif
    return -1;
}
