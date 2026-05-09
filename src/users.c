#include "users.h"
#include "fs/vfs.h"
#include "freelib/kstdio.h"
#include "freelib/kstdint.h"

static int multi_user_enabled;

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

int users_enabled(void) {
    return multi_user_enabled;
}
