# Peanut kernel ABI and boot contract

This document is the single authoritative description of the kernel–userspace boundary, boot handoff, and stable conventions. Numeric syscall constants also appear in `src/abi.h` (keep in sync when changing).

## Boot handoff

### Multiboot2

The kernel image is linked as `ELF64` with a Multiboot2 header in the first loadable segment (`src/boot/switch.asm`). The boot loader (e.g. GRUB with `multiboot2`) loads the ELF and jumps to the entry routine, which sets up an identity map of the low 4 GiB with 2 MiB pages, enables long mode, and calls `kmain`.

**Machine state at `kmain`:** long mode, paging enabled, identity mapping for `[0, 4 GiB)`, interrupts initially masked as left by firmware/loader; the kernel later installs an IDT, programs the PIC and PIT, and arms timer IRQ0 (vector 32) for scheduling.

### UEFI bundle

The in-tree UEFI artifact is a payload bundle, not firmware-native executable code:

- `make iso` builds `build/peanut.iso` for GRUB/Multiboot2.
- `make uefi-bundle` copies `build/kernel.elf` to `build/efi/BOOT/PEANUT.KRN`.
- `make isos` builds both the GRUB ISO and the EFI payload bundle.

`PEANUT.KRN` is a raw ELF64 kernel payload for a loader that already entered long mode with the paging contract described above. It is intentionally not named `BOOTX64.EFI` and should not be launched directly by firmware.

## Memory layout assumptions

- The kernel link base is **1 MiB** (`linker.ld`). The symbol `_kernel_end` marks the end of the linked image (including the `.initramfs` section if present); the bump allocator starts just after `_kernel_end` (16-byte aligned).
- Low memory is identity-mapped with **user-present** bits on all mapped pages in the bootstrap page tables (`switch.asm`), so static kernel buffers used as DMA targets or user stacks are reachable from ring 3. This is convenient for a hobby kernel but is not a security boundary.
- Common fixed regions (VGA text, ISA IDE ports, PCI MMIO including AHCI ABAR) are expected to lie inside the identity-mapped range.

## Virtual Memory And Allocation

`vm_init` installs a kernel-owned PML4 after long-mode entry. The initial VM contract keeps the low 4 GiB identity-mapped with 2 MiB pages so existing boot, DMA, VGA, PCI, and userspace addresses remain valid. `vm_map_page` can add 4 KiB mappings below the active kernel PML4, and `vm_virt_to_phys` resolves both 2 MiB identity mappings and 4 KiB page mappings.

The kernel has two allocators:

- `vm_alloc_page` returns zeroed 4 KiB pages from a page-aligned bump region after `_kernel_end`.
- `slab_alloc` serves small kernel objects from 32, 64, 128, 256, 512, and 1024 byte classes.
- `kalloc` uses slabs for allocations up to 1024 bytes and the existing large-object heap for bigger allocations.

## Initramfs

The linker defines `_initramfs_start` and `_initramfs_end` around the `.initramfs` output section. If the section is empty, both symbols are equal and `initramfs_find` finds nothing.

**Format:** ASCII **cpio “newc”** (magic `070701`). File names in the archive are matched against lookup paths with a leading `/` stripped and ASCII case folded for A–Z.

**Loader paths:** loaders may provide an image from `EFI/Peanut/initrd`, `/BOOT/INITRD`, or a Multiboot2 module, but the kernel consumes only bytes that are present in the linked `.initramfs` section.

**Build contract:** the linker always emits `_initramfs_start` / `_initramfs_end`; when the section is empty both symbols coincide and `initramfs_find` fails. To embed data, link a raw binary into `.initramfs` (for example `objcopy -I binary -O elf64-x86-64 -B i386 init.cpio initramfs.o` and add `initramfs.o` to the link line) so the section contains a **cpio newc** image. `CONFIG_INITRAMFS_EMBED` records that the kernel expects such an embedded archive.

## Init process discovery

The kernel tries, in order:

1. `SBIN/INIT` on the boot FAT32 or exFAT root volume.
2. `INIT` in the root directory.
3. `sbin/init` (case variant).

FAT32 and exFAT paths are upper-cased and stripped of a leading slash. ext paths are passed as POSIX-style absolute paths. `CONFIG_INIT_PATH` controls the first path attempted by `kmain`; `/INIT` and `/sbin/init` remain hard-coded fallbacks.

The sample init prints a startup line and remains alive by issuing `SYS_YIELD` in a loop. `SYS_EXIT` from init is treated as a fatal kernel condition.

## System calls

### Userspace convention (Linux/x86-64 style)

Userspace places the syscall number in **`RAX`** and arguments in **`RDI`**, **`RSI`**, **`RDX`**, **`R10`**, **`R8`**, **`R9`** (same ordering as Linux). The SYSCALL instruction clobbers **`RCX`** (saved user `RIP`) and **`R11`** (saved user `RFLAGS`).

### Kernel entry routine (`syscall_entry`)

The assembly entry in `src/boot/switch.asm` reorders registers before calling the C handler:

| C parameter | Source (userspace) |
|-------------|-------------------|
| `num`       | `RAX` |
| `arg1`      | `RDI` |
| `arg2`      | `RSI` |
| `arg3`      | `RDX` |
| `user_rip`  | `RCX` saved by `SYSCALL` |
| `user_rsp`  | `RSP` at syscall entry |
| `user_flags`| `R11` saved by `SYSCALL` |

`MSR_SFMASK` is set so **`IF` is cleared** on syscall entry; the PIT-driven timer does not preempt the syscall handler.

### Syscall numbers (`src/abi.h`)

| Number | Name | Behavior |
|--------|------|---------------------|
| 0 | `SYS_READ` | `read(fd, buf, count)` for opened files and pseudo files |
| 1 | `SYS_WRITE` | Writes into an opened file cache or prints a null-terminated string for fd `1`/`2` |
| 2 | `SYS_OPEN` | `open(path, flags, mode)` (flags ignored) returns fd |
| 3 | `SYS_CLOSE` | `close(fd)` releases resources |
| 4 | `SYS_ERRNO` | Returns the kernel’s last error code for the calling thread/process (generic, no errno table yet) |
| 12 | `SYS_BRK` | Bump pointer into a fixed kernel-allocated arena; see `syscall.c` |
| 24 | `SYS_YIELD` | Cooperative yield point; returns success (`0`) |
| 57 | `SYS_FORK` | Creates a runnable child process sharing the current address space; parent receives child pid, child receives `0` when scheduled |
| 59 | `SYS_EXEC` | `exec(path, argv0?, argv1?)` transfers control; uses `RDI=path, RSI=argv0, RDX=argv1` (argv0/argv1 may be null/empty) |
| 60 | `SYS_EXIT` | Prints code and panics |
| 62 | `SYS_KILL` | `kill(pid, signal)` marks a process dead for `SIGKILL`, `SIGSEGV`, or `SIGINT` |

Signals defined in `abi.h`: `PEANUT_SIGINT=2`, `PEANUT_SIGKILL=9`, and `PEANUT_SIGSEGV=11`.

### Return values and errno

There is no userspace `errno` variable. Syscalls return a **64-bit result** in `RAX`. Use **`-1` cast to `uint64_t`** (i.e. `0xFFFFFFFFFFFFFFFF`) as a generic error indicator where documented. There is no separate negative errno encoding yet.

## User process start (initial stack and segments)

`jump_to_ring3` builds an initial stack compatible with a minimal **System V x86-64** process entry:

- `SS` = `0x1B` (user data, ring 3)
- `CS` = `0x23` (user code, long mode, ring 3)
- `RFLAGS` = `0x202` (**IF** + reserved bit 1), so device interrupts may be delivered in ring 3.

Stack top is 16-byte aligned. From high to low:

- `envp` terminator / padding zeros (no environment variables are passed)
- `argv[argc]` = `NULL`
- Optional `argv[1]` (e.g. script path for shebang)
- `argv[0]` (program name or interpreter path)
- `argc` (`uint64_t`)

Thus `_start` may load **`RDI = [RSP]`** as `argc` and **`RSI = [RSP+8]`** as `argv`.

Constants: `PEANUT_USER_CS`, `PEANUT_USER_SS`, `PEANUT_USER_RFLAGS_IF` in `abi.h`.

## Shebang (`#!`)

If a file begins with `#!`, the first line is parsed for an interpreter path (optional argument portion is parsed but not yet forwarded). The kernel loads the **interpreter ELF** from the FAT/exFAT path derived from that line, and starts it with:

- `argc = 2`
- `argv[0]` = interpreter path (FAT-style)
- `argv[1]` = script path (FAT-style)

The script itself is **not** loaded into the address space; interpreters that need content open the path given in `argv[1]`.

## File I/O ABI

The kernel implements a minimal global file descriptor table. Regular files are read fully into kernel memory at `SYS_OPEN`; writes modify that in-memory copy and flush the whole file back on `SYS_CLOSE`.

### Paths

- Paths beginning with `/proc`, `/sys`, or `/dev` are handled by the pseudo filesystem layer.
- Disk filesystem paths are interpreted as:
  - FAT12/FAT16/FAT32/exFAT: “FAT-style” path components, case-folded to upper case (e.g. `/sbin/init` becomes `SBIN/INIT`).
  - ext2/3/4: standard POSIX-style absolute paths.

### `SYS_OPEN` (2)

- **Args**: `RDI=path`, `RSI=flags`, `RDX=mode` (flags/mode are accepted but ignored)
- **Return**: `RAX=fd` on success, `-1` on error.
- Regular files must already exist. Creating files, extending files, truncating files, and directory creation are not part of the syscall ABI yet.

### `SYS_READ` (0)

- **Args**: `RDI=fd`, `RSI=buf`, `RDX=count`
- **Return**: number of bytes read, `0` for EOF, `-1` on error.
- Offset advances by the number of bytes returned.

### `SYS_WRITE` (1)

- **Args**: `RDI=fd`, `RSI=buf`, `RDX=count`
- **Return**: bytes accepted, `0` when the current offset is already at EOF, `-1` on error.
- For regular files, writes are bounded by the file size captured at open time. The dirty buffer is flushed on close.
- For fd `1` and `2`, the kernel prints the userspace buffer as a C string and returns `0`.

### Filesystem write contract

- FAT12, FAT16, and FAT32 read support is handled by the FAT BPB parser. FAT12/16 use fixed root directories; FAT32 uses the root cluster chain.
- FAT12, FAT16, and FAT32 overwrite support exists through `fat32_write_file` and the `CONFIG_FS_FAT*_WRITE` options.
- exFAT write support is controlled by `CONFIG_FS_EXFAT_WRITE`; it overwrites existing file clusters without allocation or file growth.
- ext2/ext3/ext4 write support is controlled by `CONFIG_FS_EXT2_WRITE`, `CONFIG_FS_EXT3_WRITE`, and `CONFIG_FS_EXT4_WRITE`; it overwrites mapped data blocks without allocation, inode updates, or file growth.
- Metadata writes are deliberately limited: the kernel does not allocate clusters/blocks, create directory entries, update timestamps, or update file length fields.

### `SYS_CLOSE` (3)

- **Args**: `RDI=fd`
- **Return**: `0` on success, `-1` on error.

## Running a program

The kernel can start a new program image via `SYS_EXEC`:

- **Args**: `RDI=path`
- **Return**: `-1` on failure; on success control transfers to the new ring-3 entry and the syscall does not return.

The ELF loader also recognizes `#!` scripts; in that case `SYS_EXEC` runs the interpreter ELF and passes `argv[1]=script-path` as described in the shebang section.

## Process Model, Scheduling, And Faults

- The scheduler owns a fixed process table sized by `CONFIG_SCHED_MAX_PROCS`.
- `CONFIG_SCHED_RR` selects round-robin scheduling. `CONFIG_SCHED_CFS` selects a CFS-style policy that chooses the runnable process with the lowest virtual runtime.
- The timer ISR saves **15** GPRs plus the hardware **IRETQ** frame. Ring-3 timer interrupts use a **20**-quadword image: 15 GPR slots, `RIP`, `CS`, `RFLAGS`, user `RSP`, and `SS`.
- Timer-driven context switches are performed only from ring 3 frames. Ring 0 interrupts only acknowledge the PIC and return to the interrupted kernel context.
- Preemption is **armed** only after `sched_arm_user_preempt()` and `sched_mark_user_live()` immediately before the first `iretq` to userspace, so early boot and syscall handling are not switched spuriously.
- **`sched_jiffies`** (volatile `uint64_t`, defined in `sched.c`) increments on every timer IRQ for timekeeping.
- `SYS_FORK` creates a child process frame at the saved syscall return address. Processes share the current address space.
- `SYS_KILL` accepts `SIGKILL`, `SIGSEGV`, and `SIGINT` and marks the target process non-runnable.
- IDT entries for invalid opcode, general protection, and page fault have explicit handlers. Faults print vector, error code, and RIP before panicking.
- **FPU/SSE** is not saved.

## Pseudo file systems

Mount points are **logical** (no separate VFS inode tree yet). `vfs_mount_devtmpfs` mounts devtmpfs at `/dev`, and `vfs_is_pseudo_path` treats paths under `/proc`, `/sys`, `/dev`, and `/etc` as kernel-handled.

Implemented reads via `vfs_pseudo_read`:

- `/dev/zero` — zeros
- `/dev/random` — RDSEED when present, otherwise RDRAND, with timing mixed into the entropy pool
- `/dev/urandom` — RDRAND-backed stream when present, with a mixed internal state fallback
- `/dev/kbd` — nonblocking PS/2 keyboard byte stream for userspace reads
- `/dev/mouse` — nonblocking PS/2 mouse packet stream, 3 bytes per packet
- `/dev/fstab` — generated mount table for devtmpfs/proc/sysfs
- `/proc/version` — short string
- `/sys/kernel/name` — short string
- `/sys/devices/hdmi` — PCI display/HD-audio discovery status
- `/etc/passwd` — generated root and user records used to enable multi-user mode
- `/etc/fstab` — generated mount table matching `/dev/fstab`

The FAT32, exFAT, and ext4 image recipes also install `/ETC/PASSWD` and `/ETC/FSTAB` for filesystems that are inspected outside the kernel.

## Video And HDMI

The Multiboot2 header requests a 1024x768x32 framebuffer. On boot, `fb_init_from_multiboot` parses the framebuffer tag and uses `src/fonts/terminal.psf` as an 8x16 PSF font for kernel text output. Serial output remains active. `hdmi_init` scans PCI for display controllers and HD-audio HDMI functions; detected status is exposed under `/sys/devices/hdmi`.

## Stable structure sizes

Userspace should treat the following as **ABI** when using headers shared with the kernel (or duplicate layouts exactly):

- `Elf64Header`, `Elf64Phdr` in `elf.h` (standard ELF64)
- FAT directory entries are internal to the FS layer; no stable userspace exposure yet.

## GDT / TSS

Selectors: kernel code `0x08`, kernel data `0x10`, user data `0x18` (use `0x1B` with RPL 3), user code `0x20` (use `0x23` with RPL 3), TSS `0x28`. `kernel_tss.rsp0` points at the syscall/interrupt kernel stack (`gdt.c`).
