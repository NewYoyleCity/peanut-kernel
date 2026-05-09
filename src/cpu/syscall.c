#include "freelib/kstdio.h"
#include "freelib/kstdint.h"
#include "freelib/kpanic.h"
#include "abi.h"
#include "config.h"
#include "storage.h"
#include "fs/vfs.h"
#include "freelib/kalloc.h"
#include "programs/elf.h"
#include "cpu/sched.h"

#ifdef CONFIG_NET_TCP_IP
#include "drivers/net/net.h"
#endif

#ifdef CONFIG_NETWORK_SYSCALLS
typedef uint32_t socklen_t;
#endif

#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_SFMASK 0xC0000084

#define EFER_SCE (1 << 0)
#define RFLAGS_IF (1 << 9)

extern void syscall_entry(void);

#define USER_BRK_ARENA_BYTES (256u * 1024u)
static uint8_t user_brk_arena[USER_BRK_ARENA_BYTES] __attribute__((aligned(4096)));
static uintptr_t user_brk_end;


#define FD_MAX 16
typedef struct {
    int used;
    uint8_t* data;
    uint32_t size;
    uint32_t off;
    char path[160];
    int is_pseudo;
    int dirty;
    int is_socket;
    int socket_domain;
    int socket_type;
    int socket_protocol;
} Fd;

static Fd fds[FD_MAX];

static uint64_t last_errno;

static uint64_t rdmsr(uint32_t msr) {
    uint32_t low;
    uint32_t high;

    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;

    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

void syscall_init(void) {
    user_brk_end = (uintptr_t)user_brk_arena;

    last_errno = 0;

    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | EFER_SCE);

    wrmsr(MSR_STAR, ((uint64_t)0x13 << 48) | ((uint64_t)0x08 << 32));
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    wrmsr(MSR_SFMASK, RFLAGS_IF);
}

static uint64_t handle_brk(uintptr_t addr) {
    uintptr_t base = (uintptr_t)user_brk_arena;
    uintptr_t limit = base + USER_BRK_ARENA_BYTES;

    if (addr == 0)
        return (uint64_t)user_brk_end;

    if (addr < base || addr > limit)
        return (uint64_t)-1;

    user_brk_end = addr;
    return (uint64_t)user_brk_end;
}


static void kstrcpy(char* d, const char* s, uint32_t cap) {
    uint32_t i = 0;
    for (; i + 1 < cap && s[i]; i++) d[i] = s[i];
    d[i] = '\0';
}

static void fs_path_to_fat_name(const char* path, char* out, uint32_t cap) {
    uint32_t j = 0;
    uint32_t i = (path && path[0] == '/') ? 1 : 0;
    if (!path || cap == 0)
        return;
    while (path[i] && j + 1 < cap) {
        char c = path[i++];
        out[j++] = (char)((c >= 'a' && c <= 'z') ? (char)(c - 32) : c);
    }
    out[j] = '\0';
}

static void copy_user_cstr(char* dst, const char* user_ptr, uint32_t cap) {
    if (!user_ptr) {
        dst[0] = '\0';
        return;
    }
    uint32_t i = 0;
    while (i + 1 < cap) {
        char c = user_ptr[i];
        dst[i] = c;
        if (!c) break;
        i++;
    }
    dst[cap - 1] = '\0';
}

static int alloc_fd(void) {
    for (int i = 0; i < FD_MAX; i++) {
        if (!fds[i].used) {
            fds[i].used = 1;
            fds[i].data = 0;
            fds[i].size = 0;
            fds[i].off = 0;
            fds[i].is_pseudo = 0;
            fds[i].is_socket = 0;
            fds[i].path[0] = '\0';
            return i;
        }
    }
    return -1;
}

static int read_file_into_mem(const char* path, uint8_t** out, uint32_t* out_sz) {
    PeanutVolume* vol = storage_get_root_volume();
    if (!vol) return -1;
    if (vfs_is_pseudo_path(path)) {
        return -2;
    }
    uint64_t size64 = 0;
    if (vol->fs_kind == PEANUT_FS_FAT32) {
        char fat[160];
        fs_path_to_fat_name(path, fat, sizeof(fat));
        Fat32DirEntry e;
        if (fat32_find_root(&vol->fat32, fat, &e) != 0) return -1;
        if (e.attributes & FAT32_ATTR_DIRECTORY) return -1;
        size64 = e.size;
        uint8_t* buf = kalloc((size_t)size64);
        if (!buf) return -1;
        uint32_t br = 0;
        if (fat32_read_file(&vol->fat32, fat, buf, (uint32_t)size64, &br) != 0 || br != (uint32_t)size64) {
            kfree(buf);
            return -1;
        }
        *out = buf;
        *out_sz = (uint32_t)size64;
        return 0;
    }
    if (vol->fs_kind == PEANUT_FS_EXFAT) {
        char ex[160];
        fs_path_to_fat_name(path, ex, sizeof(ex));
        ExfatDirEntry e;
        if (exfat_find_root(&vol->exfat, ex, &e) != 0) return -1;
        if (e.attributes & EXFAT_ATTR_DIRECTORY) return -1;
        size64 = e.size;
        if (size64 > 0xFFFFFFFFu) return -1;
        uint8_t* buf = kalloc((size_t)size64);
        if (!buf) return -1;
        uint32_t br = 0;
        if (exfat_read_file(&vol->exfat, ex, buf, (uint32_t)size64, &br) != 0 || br != (uint32_t)size64) {
            kfree(buf);
            return -1;
        }
        *out = buf;
        *out_sz = (uint32_t)size64;
        return 0;
    }
    char p[160];
    if (path[0] != '/') {
        p[0] = '/';
        kstrcpy(p + 1, path, sizeof(p) - 1);
    } else {
        kstrcpy(p, path, sizeof(p));
    }
    int is_dir = 0;
    if (extfs_stat(&vol->ext, p, &size64, &is_dir) != 0) return -1;
    if (is_dir) return -1;
    if (size64 > 0xFFFFFFFFu) return -1;
    uint8_t* buf = kalloc((size_t)size64);
    if (!buf) return -1;
    uint32_t br = 0;
    if (extfs_read_file(&vol->ext, p, buf, (uint32_t)size64, &br) != 0 || br != (uint32_t)size64) {
        kfree(buf);
        return -1;
    }
    *out = buf;
    *out_sz = (uint32_t)size64;
    return 0;
}

uint64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3,
                         uint64_t user_rip, uint64_t user_rsp, uint64_t user_flags) {
    last_errno = 0;

    switch (num) {
        case SYS_READ:
            if (arg1 < FD_MAX && fds[arg1].used) {
                Fd* f = &fds[arg1];
                uint8_t* dst = (uint8_t*)arg2;
                uint32_t want = (uint32_t)arg3;
                if (f->is_pseudo) {
                    uint32_t got = 0;
                    if (vfs_pseudo_pread(f->path, f->off, dst, want, &got) != 0) {
                        last_errno = 1;
                        return (uint64_t)-1;
                    }
                    f->off += got;
                    return got;
                }
                if (f->off >= f->size)
                    return 0;
                uint32_t left = f->size - f->off;
                uint32_t take = want < left ? want : left;
                for (uint32_t i = 0; i < take; i++)
                    dst[i] = f->data[f->off + i];
                f->off += take;
                return take;
            }
            last_errno = 2;
            return (uint64_t)-1;

        case SYS_WRITE:
            if (arg1 < FD_MAX && fds[arg1].used) {
                Fd* f = &fds[arg1];
                if (f->is_pseudo) {
                    last_errno = 8;
                    return (uint64_t)-1;
                }
                if (!f->data) {
                    last_errno = 9;
                    return (uint64_t)-1;
                }

                uint8_t* src = (uint8_t*)arg2;
                uint32_t want = (uint32_t)arg3;

                if (f->off >= f->size)
                    return 0;

                uint32_t left = f->size - f->off;
                uint32_t take = want < left ? want : left;
                for (uint32_t i = 0; i < take; i++)
                    f->data[f->off + i] = src[i];

                f->off += take;
                f->dirty = 1;
                return take;
            }

            if (arg1 == 1 || arg1 == 2) {
                kprint((const char*)arg2);
                return 0;
            }

            last_errno = 10;
            return (uint64_t)-1;

        case SYS_ERRNO:
            return last_errno;

        case SYS_FORK: {
            int pid = sched_fork_current(user_rip, user_rsp, user_flags);
            if (pid < 0) {
                last_errno = 12;
                return (uint64_t)-1;
            }
            return (uint64_t)pid;
        }

        case SYS_YIELD:
            return 0;

        case SYS_BRK:
            return handle_brk((uintptr_t)arg1);

        case SYS_OPEN: {
            const char* path = (const char*)arg1;
            (void)arg2;
            (void)arg3;
            int fd = alloc_fd();
            if (fd < 0) {
                last_errno = 3;
                return (uint64_t)-1;
            }
            if (vfs_is_pseudo_path(path)) {
                fds[fd].is_pseudo = 1;
                fds[fd].dirty = 0;
                kstrcpy(fds[fd].path, path, sizeof(fds[fd].path));
                return (uint64_t)fd;
            }
            fds[fd].is_pseudo = 0;
            fds[fd].dirty = 0;
            kstrcpy(fds[fd].path, path, sizeof(fds[fd].path));

            uint8_t* mem = 0;
            uint32_t sz = 0;
            int r = read_file_into_mem(path, &mem, &sz);
            if (r != 0) {
                fds[fd].used = 0;
                last_errno = 4;
                return (uint64_t)-1;
            }
            fds[fd].data = mem;
            fds[fd].size = sz;
            fds[fd].off = 0;
            return (uint64_t)fd;
        }

        case SYS_CLOSE:
            if (arg1 < FD_MAX && fds[arg1].used) {
                Fd* f = &fds[arg1];

                if (!f->is_pseudo && f->data) {
                    if (f->dirty && f->path[0] != '\0') {
                        PeanutVolume* vol = storage_get_root_volume();
                        if (vol) {
                            int wr = -1;
                            if (vol->fs_kind == PEANUT_FS_EXT) {
#if defined(CONFIG_FS_EXT4_WRITE) || defined(CONFIG_FS_EXT3_WRITE) || defined(CONFIG_FS_EXT2_WRITE)
                                char p[160];
                                if (f->path[0] == '/') kstrcpy(p, f->path, sizeof(p));
                                else {
                                    p[0] = '/';
                                    kstrcpy(p + 1, f->path, sizeof(p) - 1);
                                }
                                wr = extfs_write_file(&vol->ext, p, f->data, f->size);
#endif
                            } else if (vol->fs_kind == PEANUT_FS_EXFAT) {
#ifdef CONFIG_FS_EXFAT_WRITE
                                char p[160];
                                fs_path_to_fat_name(f->path, p, sizeof(p));
                                wr = exfat_write_file(&vol->exfat, p, f->data, f->size);
#endif
                            }
                            if (wr != 0)
                                last_errno = 11;
                        }
                    }
                    kfree(f->data);
                }

                f->used = 0;
                return 0;
            }
            last_errno = 5;
            return (uint64_t)-1;

        case SYS_EXEC: {
            PeanutVolume* vol = storage_get_root_volume();
            if (!vol) {
                last_errno = 6;
                return (uint64_t)-1;
            }

            const char* path_in = (const char*)arg1;
            const char* argv0_in = (const char*)arg2;
            const char* argv1_in = (const char*)arg3;

            if (!path_in) {
                last_errno = 7;
                return (uint64_t)-1;
            }

            char path_buf[160];
            char argv0_buf[160];
            char argv1_buf[160];
            copy_user_cstr(path_buf, path_in, sizeof(path_buf));
            copy_user_cstr(argv0_buf, argv0_in, sizeof(argv0_buf));
            copy_user_cstr(argv1_buf, argv1_in, sizeof(argv1_buf));

            const char* exec_path = path_buf;
            const char* exec_argv0 = (argv0_buf[0] != '\0') ? argv0_buf : exec_path;
            const char* exec_argv1 = (argv1_buf[0] != '\0') ? argv1_buf : NULL;

            (void)elf_load_and_run_exec(vol, exec_path, exec_argv0, exec_argv1);
            return (uint64_t)-1;
        }

        case SYS_KILL:
            if (sched_kill((int)arg1, (int)arg2) != 0) {
                last_errno = 13;
                return (uint64_t)-1;
            }
            return 0;

        case SYS_EXIT:
            kprint("\n[Process exited with code: ");
            kprint_int(arg1);
            kprint("]\n");
            kpanic("[Peanut kernel - panic - Attempted to kill init!]");

#ifdef CONFIG_NETWORK_SYSCALLS
        case SYS_SOCKET: {
            int domain = (int)arg1;
            int type = (int)arg2;
            int protocol = (int)arg3;
            
            int fd = alloc_fd();
            if (fd < 0) {
                last_errno = 20;
                return (uint64_t)-1;
            }
            
            fds[fd].is_socket = 1;
            fds[fd].socket_domain = domain;
            fds[fd].socket_type = type;
            fds[fd].socket_protocol = protocol;
            fds[fd].data = kalloc(4096);
            fds[fd].size = 4096;
            
            return (uint64_t)fd;
        }

        case SYS_BIND: {
            int sockfd = (int)arg1;
            sockaddr_t *addr = (sockaddr_t *)arg2;
            socklen_t addrlen = (socklen_t)arg3;
            
            if (sockfd < 0 || sockfd >= FD_MAX || !fds[sockfd].used || !fds[sockfd].is_socket) {
                last_errno = 21;
                return (uint64_t)-1;
            }
            
            (void)addr;
            (void)addrlen;
            
            return 0;
        }

        case SYS_LISTEN: {
            int sockfd = (int)arg1;
            int backlog = (int)arg2;
            
            if (sockfd < 0 || sockfd >= FD_MAX || !fds[sockfd].used || !fds[sockfd].is_socket) {
                last_errno = 22;
                return (uint64_t)-1;
            }
            
            (void)backlog;
            
            return 0;
        }

        case SYS_ACCEPT: {
            int sockfd = (int)arg1;
            sockaddr_t *addr = (sockaddr_t *)arg2;
            socklen_t *addrlen = (socklen_t *)arg3;
            
            if (sockfd < 0 || sockfd >= FD_MAX || !fds[sockfd].used || !fds[sockfd].is_socket) {
                last_errno = 23;
                return (uint64_t)-1;
            }
            
            (void)addr;
            (void)addrlen;
            
            int newfd = alloc_fd();
            if (newfd < 0) {
                last_errno = 20;
                return (uint64_t)-1;
            }
            
            fds[newfd].is_socket = 1;
            fds[newfd].socket_domain = fds[sockfd].socket_domain;
            fds[newfd].socket_type = fds[sockfd].socket_type;
            fds[newfd].socket_protocol = fds[sockfd].socket_protocol;
            fds[newfd].data = kalloc(4096);
            fds[newfd].size = 4096;
            
            return (uint64_t)newfd;
        }

        case SYS_CONNECT: {
            int sockfd = (int)arg1;
            sockaddr_t *addr = (sockaddr_t *)arg2;
            socklen_t addrlen = (socklen_t)arg3;
            
            if (sockfd < 0 || sockfd >= FD_MAX || !fds[sockfd].used || !fds[sockfd].is_socket) {
                last_errno = 24;
                return (uint64_t)-1;
            }
            
            (void)addr;
            (void)addrlen;
            
            return 0;
        }

        case SYS_SEND: {
            int sockfd = (int)arg1;
            void *buf = (void *)arg2;
            size_t len = (size_t)arg3;
            int flags = (int)user_rip;
            
            if (sockfd < 0 || sockfd >= FD_MAX || !fds[sockfd].used || !fds[sockfd].is_socket) {
                last_errno = 25;
                return (uint64_t)-1;
            }
            
            (void)buf;
            (void)len;
            (void)flags;
            
#ifdef CONFIG_NET_TCP_IP
            net_device_t *dev = net_get_first_device();
            if (dev && dev->send) {
                dev->send(dev, (uint8_t *)buf, len);
                return len;
            }
#endif
            
            return (uint64_t)-1;
        }

        case SYS_RECV: {
            int sockfd = (int)arg1;
            void *buf = (void *)arg2;
            size_t len = (size_t)arg3;
            int flags = (int)user_rip;
            
            if (sockfd < 0 || sockfd >= FD_MAX || !fds[sockfd].used || !fds[sockfd].is_socket) {
                last_errno = 26;
                return (uint64_t)-1;
            }
            
            (void)buf;
            (void)len;
            (void)flags;
            
            return 0;
        }

        case SYS_GETADDRINFO: {
            const char *node = (const char *)arg1;
            const char *service = (const char *)arg2;
            
            (void)node;
            (void)service;
            
            last_errno = 27;
            return (uint64_t)-1;
        }
#endif

        default:
            kprint("Unknown syscall: ");
            kprint_int(num);
            kprint("\n");
            return (uint64_t)-1;
    }
}
