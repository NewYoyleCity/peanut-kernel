#include "programs/elf.h"
#include "freelib/kstdint.h"
#include "freelib/kstdio.h"
#include "freelib/kalloc.h"
#include "freelib/kpanic.h"
#include "abi.h"
#include "cpu/sched.h"

static void kstrlcpy(char* d, const char* s, size_t cap) {
    size_t i;
    for (i = 0; i + 1 < cap && s[i]; i++)
        d[i] = s[i];
    d[i] = '\0';
}

static int parse_shebang_line(const uint8_t* data, uint32_t len, char* interp, uint32_t interp_sz,
                              char* arg, uint32_t arg_sz) {
    if (len < 2 || data[0] != '#' || data[1] != '!')
        return -1;
    uint32_t i = 2;
    while (i < len && (data[i] == ' ' || data[i] == '\t'))
        i++;
    uint32_t is = 0;
    while (i < len && data[i] != '\n' && data[i] != '\r') {
        if (data[i] == ' ' || data[i] == '\t')
            break;
        if (is + 1 >= interp_sz)
            return -1;
        interp[is++] = (char)data[i++];
    }
    interp[is] = '\0';
    if (is == 0)
        return -1;
    while (i < len && (data[i] == ' ' || data[i] == '\t'))
        i++;
    uint32_t as = 0;
    while (i < len && data[i] != '\n' && data[i] != '\r') {
        if (as + 1 >= arg_sz)
            break;
        arg[as++] = (char)data[i++];
    }
    arg[as] = '\0';
    return 0;
}

static void fat_path_from_fs(const char* fs_path, char* out, uint32_t out_sz) {
    uint32_t j = 0;
    if (fs_path[0] == '/') {
        uint32_t i = 1;
        while (fs_path[i] && j + 1 < out_sz) {
            char c = fs_path[i++];
            out[j++] = (char)((c >= 'a' && c <= 'z') ? (char)(c - 32) : c);
        }
    } else {
        uint32_t i = 0;
        while (fs_path[i] && j + 1 < out_sz) {
            char c = fs_path[i++];
            out[j++] = (char)((c >= 'a' && c <= 'z') ? (char)(c - 32) : c);
        }
    }
    out[j] = '\0';
}

static int elf_load_and_run_inner(PeanutVolume* vol, const char* path, const char* argv0,
                                  const char* argv1_opt);

int elf_load_and_run(PeanutVolume* vol, const char* path) {
    return elf_load_and_run_inner(vol, path, path, NULL);
}

int elf_load_and_run_exec(PeanutVolume* vol, const char* path, const char* argv0, const char* argv1_opt) {
    return elf_load_and_run_inner(vol, path, argv0 ? argv0 : path, argv1_opt);
}

static int elf_load_and_run_inner(PeanutVolume* vol, const char* path, const char* argv0,
                                  const char* argv1_opt) {
    uint64_t file_size = 0;

    if (vol->fs_kind == PEANUT_FS_FAT32) {
        char fatp[160];
        const char* pth = path;
        if (path[0] == '/') {
            fat_path_from_fs(path, fatp, sizeof(fatp));
            pth = fatp;
        }
        Fat32DirEntry file_info;
        if (fat32_find_root(&vol->fat32, pth, &file_info) != 0)
            return -1;
        if (file_info.attributes & FAT32_ATTR_DIRECTORY)
            return -1;
        file_size = file_info.size;
    } else if (vol->fs_kind == PEANUT_FS_EXFAT) {
        char fatp[160];
        const char* pth = path;
        if (path[0] == '/') {
            fat_path_from_fs(path, fatp, sizeof(fatp));
            pth = fatp;
        }
        ExfatDirEntry file_info;
        if (exfat_find_root(&vol->exfat, pth, &file_info) != 0)
            return -1;
        if (file_info.attributes & EXFAT_ATTR_DIRECTORY)
            return -1;
        file_size = file_info.size;
    } else {
        char p[160];
        if (path[0] != '/') {
            p[0] = '/';
            kstrlcpy(p + 1, path, sizeof(p) - 1);
        } else {
            kstrlcpy(p, path, sizeof(p));
        }
        uint64_t sz = 0;
        int is_dir = 0;
        if (extfs_stat(&vol->ext, p, &sz, &is_dir) != 0)
            return -1;
        if (is_dir)
            return -1;
        file_size = sz;
    }

    if (file_size == 0 || file_size > (uint64_t)(1024u * 1024u * 16u))
        return -1;

    uint8_t* elf_buffer = kalloc((size_t)file_size);
    if (!elf_buffer)
        return -1;

    uint32_t bytes_read = 0;
    int rd;
    if (vol->fs_kind == PEANUT_FS_FAT32) {
        char fatp[160];
        const char* pth = path;
        if (path[0] == '/') {
            fat_path_from_fs(path, fatp, sizeof(fatp));
            pth = fatp;
        }
        rd = fat32_read_file(&vol->fat32, pth, elf_buffer, (uint32_t)file_size, &bytes_read);
    } else if (vol->fs_kind == PEANUT_FS_EXFAT) {
        char fatp[160];
        const char* pth = path;
        if (path[0] == '/') {
            fat_path_from_fs(path, fatp, sizeof(fatp));
            pth = fatp;
        }
        rd = exfat_read_file(&vol->exfat, pth, elf_buffer, (uint32_t)file_size, &bytes_read);
    }
    else {
        char p[160];
        if (path[0] != '/') {
            p[0] = '/';
            kstrlcpy(p + 1, path, sizeof(p) - 1);
        } else {
            kstrlcpy(p, path, sizeof(p));
        }
        rd = extfs_read_file(&vol->ext, p, elf_buffer, (uint32_t)file_size, &bytes_read);
    }

    if (rd != 0 || bytes_read != (uint32_t)file_size) {
        kfree(elf_buffer);
        return -1;
    }

    if (bytes_read >= 2 && elf_buffer[0] == '#' && elf_buffer[1] == '!') {
        char interp[128];
        char arg[128];
        if (parse_shebang_line(elf_buffer, bytes_read, interp, sizeof(interp), arg, sizeof(arg)) != 0) {
            kfree(elf_buffer);
            return -1;
        }
        kfree(elf_buffer);
        char interp_fat[144];
        char script_fat[144];
        const char* interp_path = interp;
        const char* script_path = path;
        if (vol->fs_kind != PEANUT_FS_EXT) {
            fat_path_from_fs(interp, interp_fat, sizeof(interp_fat));
            fat_path_from_fs(path, script_fat, sizeof(script_fat));
            interp_path = interp_fat;
            script_path = script_fat;
        }
        (void)arg;
        return elf_load_and_run_inner(vol, interp_path, interp_path, script_path);
    }

    Elf64Header* header = (Elf64Header*)elf_buffer;
    if (*(uint32_t*)header->e_ident != ELF_MAGIC) {
        kfree(elf_buffer);
        return -1;
    }

    Elf64Phdr* phdr = (Elf64Phdr*)(elf_buffer + header->e_phoff);
    
    // Check for PT_INTERP (dynamic linker)
    const char* interp_path = NULL;
    for (int i = 0; i < header->e_phnum; i++) {
        if (phdr[i].p_type == PT_INTERP) {
            interp_path = (const char*)(elf_buffer + phdr[i].p_offset);
            break;
        }
    }
    
    // If dynamic linker is specified, load it instead of the program directly
    if (interp_path) {
        char interp_copy[256];
        kstrlcpy(interp_copy, interp_path, sizeof(interp_copy));
        kfree(elf_buffer);
        
        // Load the dynamic linker, passing the original program as argv[1]
        char interp_fat[144];
        const char* interp_path_fat = interp_copy;
        if (vol->fs_kind != PEANUT_FS_EXT) {
            fat_path_from_fs(interp_copy, interp_fat, sizeof(interp_fat));
            interp_path_fat = interp_fat;
        }
        
        return elf_load_and_run_inner(vol, interp_path_fat, interp_path_fat, path);
    }
    
    // Load PT_LOAD segments
    for (int i = 0; i < header->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            uint8_t* dest = (uint8_t*)phdr[i].p_vaddr;
            uint8_t* src = elf_buffer + phdr[i].p_offset;

            for (uint64_t j = 0; j < phdr[i].p_filesz; j++) {
                dest[j] = src[j];
            }
            if (phdr[i].p_memsz > phdr[i].p_filesz) {
                for (uint64_t j = phdr[i].p_filesz; j < phdr[i].p_memsz; j++) {
                    dest[j] = 0;
                }
            }
        }
    }

    uint64_t entry_point = header->e_entry;
    kfree(elf_buffer);

    jump_to_ring3(entry_point, argv0, argv1_opt);

    return -1;
}

void jump_to_ring3(uint64_t entry_point, const char* argv0, const char* argv1_opt) {
    static uint8_t test_user_stack[4096];
    uintptr_t top = (uintptr_t)(test_user_stack + sizeof(test_user_stack));
    top &= ~15ull;
    char* str_base = (char*)top - 512;
    kstrlcpy(str_base, argv0 ? argv0 : "", 256);
    char* p0 = str_base;
    char* p1 = NULL;
    if (argv1_opt && argv1_opt[0]) {
        kstrlcpy(str_base + 256, argv1_opt, 256);
        p1 = str_base + 256;
    }
    uint64_t argc = p1 ? 2ull : 1ull;
    uint64_t* sp = (uint64_t*)top;
    sp--;
    *sp = 0;
    sp--;
    *sp = 0;
    sp--;
    *sp = 0;
    if (p1) {
        sp--;
        *sp = (uint64_t)p1;
    }
    sp--;
    *sp = (uint64_t)p0;
    sp--;
    *sp = argc;
    sp = (uint64_t*)((uintptr_t)sp & ~15ull);
    uint64_t rsp = (uint64_t)sp;

    if (sched_create_user(entry_point, rsp) < 0)
        kpanic("[Peanut kernel - panic - could not create init process]");
    sched_arm_user_preempt();
    sched_mark_user_live();

    __asm__ volatile(
        "mov $0x1B, %%ax \n"
        "mov %%ax, %%ds \n"
        "mov %%ax, %%es \n"
        "mov %%ax, %%fs \n"
        "mov %%ax, %%gs \n"
        "pushq $0x1B \n"
        "pushq %0 \n"
        "pushq $0x202 \n"
        "pushq $0x23 \n"
        "pushq %1 \n"
        "iretq \n"
        : : "r"(rsp), "r"(entry_point)
        : "rax", "memory");
}
