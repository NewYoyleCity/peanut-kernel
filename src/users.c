/* users.c -- Minimal multi-user support via /etc/passwd.
 *
 * Reads the pseudo-file /etc/passwd at boot.  If present and non-empty,
 * multi-user mode is enabled; otherwise the system runs as single-user.
 * This is intentionally simplistic -- no user switching, no groups. */

#include "users.h"
#include "fs/vfs.h"
#include "freelib/kstdio.h"
#include "freelib/kstdint.h"

static int multi_user_enabled;


/* users_init -- read /etc/passwd from the VFS pseudo-filesystem.
 * If non-empty, multi-user mode is enabled.
 */
void users_init(void) {
    uint8_t buf[256];
    uint32_t got = 0;
    multi_user_enabled = 0;
    if (vfs_pseudo_read("/etc/passwd", buf, sizeof(buf) - 1, &got) == 0 && got > 0) {
        buf[got] = 0;
        multi_user_enabled = 1;
        kprint("Multi-user: /etc/passwd loaded\n");
    }
}


/* users_enabled -- return 1 if multi-user mode is active.
 */
int users_enabled(void) {
    return multi_user_enabled;
}
