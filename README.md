# Peanut Kernel

Peanut Kernel is a small x86-64 hobby kernel with a Multiboot2 boot path, a
minimal userspace ABI, block-device discovery, filesystem loading, and a sample
ring-3 init program.

## What It Builds

- `build/kernel.elf`: the Multiboot2 ELF64 kernel image.
- `build/peanut.iso`: a GRUB ISO that boots `kernel.elf`.
- `disk.img`: a FAT32 root disk containing `/SBIN/INIT` and basic FHS folders.
- `disk-exfat.img`: an exFAT root disk populated with the sample init.
- `disk-ext4.img`: an ext4 root disk populated with the sample init.
- `build/efi/BOOT/PEANUT.KRN`: an ELF64 payload for a compatible UEFI loader.

## Main Capabilities

- Long-mode x86-64 boot with GDT, IDT, PIC, PIT, and syscall entry setup.
- Round-robin scheduling with fixed-size process storage.
- Ring-3 ELF loading, `exec`, `fork`, `yield`, `exit`, `kill`, and basic file I/O.
- FAT12, FAT16, FAT32, exFAT, ext2, ext3, ext4, ISO9660, initramfs, and pseudo filesystem code.
- AHCI and IDE block discovery, plus storage image recipes for FAT32, exFAT, and ext4.
- VGA/framebuffer text output, PS/2 input, entropy devices, and PCI device discovery.
- E1000 and RTL8139 packet drivers, plus Realtek PCIe and virtio-net discovery.
- AC97, HDA, USB xHCI, and HDMI/display discovery paths.

## Build Requirements

The default build expects:

- `gcc`, `ld`, `nasm`, `make`
- `grub-mkrescue` for `make iso`
- `qemu-system-x86_64` for `make run`
- `mtools`, `dosfstools`, `sfdisk`, `dd` for `disk.img`
- `exfatprogs` for `disk-exfat.img`
- `e2fsprogs` for `disk-ext4.img`

## Common Commands

```sh
make defconfig
make all
make iso
make disk.img
make run
```

`make defconfig` writes `.config` and `include/generated/autoconf.h`. Kernel
sources include those generated feature switches through `src/config.h`.

## Documentation

The boot contract, syscall ABI, userspace stack layout, filesystem behavior,
scheduler frame layout, pseudo files, and stable structure notes are documented
in `docs/KERNEL_ABI.md`.
