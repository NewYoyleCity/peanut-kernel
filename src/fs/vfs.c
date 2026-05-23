/* vfs.c -- Minimal Virtual File System layer.
 *
 * Provides pseudo-files (/proc, /sys, /dev, /etc) served by in-kernel
 * string generators, and delegates physical filesystem access to the
 * storage layer.  The devtmpfs is mounted at /dev for device nodes.
 */

#include "fs/vfs.h"
#include "freelib/kstdint.h"
#include "drivers/entropy.h"
#include "fs/devtmpfs.h"
#include "drivers/video/hdmi.h"

static int dev_mounted;


/* path_has_prefix -- test if path starts with a prefix at a component boundary.
 */static int path_has_prefix(const char* path, const char* prefix) {
    uint32_t i = 0;
    for (;; i++) {
        if (prefix[i] == '\0')
            return path[i] == '\0' || path[i] == '/';
        if (path[i] != prefix[i])
            return 0;
    }
}


/* vfs_init -- initialise the VFS layer (currently seeds entropy pool).
 */
void vfs_init(void) {
    entropy_init();
}


/* vfs_mount_devtmpfs -- mount the device temporary filesystem at /dev.
 */
void vfs_mount_devtmpfs(void) {
    devtmpfs_mount();
    dev_mounted = 1;
}


/* vfs_is_pseudo_path -- return 1 if the path lives under /proc, /sys, /dev, or /etc.
 */
int vfs_is_pseudo_path(const char* path) {
    if (!path || path[0] != '/')
        return 0;
    if (path_has_prefix(path, "/proc") || path_has_prefix(path, "/sys") ||
        path_has_prefix(path, "/dev") || path_has_prefix(path, "/etc"))
        return 1;
    return 0;
}


/* pseudo_copy -- copy a substring from a static string.
 */static int pseudo_copy(const char* s, uint32_t off, uint8_t* buf, uint32_t len, uint32_t* out) {
    uint32_t sl = 0;
    while (s[sl]) sl++;
    if (off >= sl) {
        *out = 0;
        return 0;
    }
    uint32_t take = sl - off;
    if (take > len) take = len;
    for (uint32_t i = 0; i < take; i++) buf[i] = (uint8_t)s[off + i];
    *out = take;
    return 0;
}


/* vfs_pseudo_pread -- read from a pseudo-path with offset support.
 */
int vfs_pseudo_pread(const char* path, uint32_t off, uint8_t* buf, uint32_t len, uint32_t* out) {
    if (!path || !buf || !out)
        return -1;
    *out = 0;

    if (dev_mounted && devtmpfs_is_path(path))
        return devtmpfs_pread(path, off, buf, len, out);
    if (path_has_prefix(path, "/proc/version")) {
        const char* s = "Peanut kernel pseudo proc\n";
        return pseudo_copy(s, off, buf, len, out);
    }
    if (path_has_prefix(path, "/sys/kernel/name")) {
        const char* s = "Peanut\n";
        return pseudo_copy(s, off, buf, len, out);
    }
    if (path_has_prefix(path, "/sys/devices/hdmi")) {
        return pseudo_copy(hdmi_status(), off, buf, len, out);
    }
    if (path_has_prefix(path, "/etc/passwd")) {
        const char* s = "root:x:0:0:root:/root:/sbin/init\nuser:x:1000:1000:Peanut User:/home/user:/bin/sh\n";
        return pseudo_copy(s, off, buf, len, out);
    }
    if (path_has_prefix(path, "/etc/fstab")) {
        const char* s = "devtmpfs /dev devtmpfs rw 0 0\nproc /proc proc ro 0 0\nsysfs /sys sysfs ro 0 0\n";
        return pseudo_copy(s, off, buf, len, out);
    }
    return -1;
}


/* vfs_pseudo_pwrite -- write to a pseudo-path with offset support.
 */
int vfs_pseudo_pwrite(const char* path, uint32_t off, const uint8_t* buf, uint32_t len, uint32_t* out) {
    if (!path || !buf || !out)
        return -1;
    *out = 0;
    if (dev_mounted && devtmpfs_is_path(path))
        return devtmpfs_pwrite(path, off, buf, len, out);
    return -1;
}


/* vfs_pseudo_read -- read from a pseudo-path at offset 0.
 */
int vfs_pseudo_read(const char* path, uint8_t* buf, uint32_t len, uint32_t* out) {
    return vfs_pseudo_pread(path, 0, buf, len, out);
}
