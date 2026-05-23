CC = gcc
CXX = g++
AS = nasm
LD = ld

KCONFIG_HEADER = include/generated/autoconf.h

CFLAGS = -ffreestanding -mcmodel=kernel -mno-red-zone -m64 -mno-sse -mno-sse2 \
         -fno-pic -fno-pie -fcf-protection=none \
         -Iinclude -Iinclude/generated -Isrc
CXXFLAGS = -ffreestanding -mcmodel=kernel -mno-red-zone -m64 -mno-sse -mno-sse2 \
           -fno-pic -fno-pie -fcf-protection=none \
           -fno-exceptions -fno-rtti -fno-threadsafe-statics \
           -Iinclude -Iinclude/generated -Isrc

ifeq ($(CONFIG_STACK_CANARY),y)
  CFLAGS += -fstack-protector-strong
  CXXFLAGS += -fstack-protector-strong
else
  CFLAGS += -fno-stack-protector
  CXXFLAGS += -fno-stack-protector
endif
ASFLAGS = -f elf64
LDFLAGS = -n -T linker.ld -z max-page-size=0x1000 --no-warn-rwx-segments -z noexecstack \
          -nostdlib

# --- Conditional sources based on .config ---
-include .config

SRCS_C := \
    src/main.c \
    src/storage.c \
    src/users.c \
    src/freelib/kpanic.c \
    src/freelib/kstdio.c \
    src/freelib/kalloc.c \
    src/freelib/slab.c \
    src/freelib/stack_protector.c \
    src/cpu/gdt.c \
    src/cpu/idt.c \
    src/cpu/pic.c \
    src/cpu/pit.c \
    src/cpu/sched.c \
    src/cpu/syscall.c \
    src/mm/vm.c \
    src/fs/vfs.c \
    src/fs/devtmpfs.c \
    src/fs/partition.c \
    src/fs/initramfs.c \
    src/drivers/block/block.c \
    src/drivers/bus/pci.c \
    src/drivers/entropy.c \
    src/drivers/video/fb.c \
    src/drivers/video/psf_font.c \
    src/lib/inflate.c \
    src/programs/elf.c

# Device Drivers
ifeq ($(CONFIG_STORAGE_IDE),y)
  SRCS_C += src/drivers/block/ide.c
endif
ifeq ($(CONFIG_STORAGE_AHCI),y)
  SRCS_C += src/drivers/block/ahci.c
endif
ifeq ($(CONFIG_STORAGE_NVME),y)
  SRCS_C += src/drivers/block/nvme.c
endif
ifeq ($(CONFIG_STORAGE_CDROM),y)
  SRCS_C += src/drivers/block/cdrom.c src/fs/iso9660.c
endif
ifeq ($(CONFIG_STORAGE_FLOPPY),y)
  SRCS_C += src/drivers/block/floppy.c src/fs/fat12.c
endif
ifeq ($(CONFIG_BLOCK_RAID),y)
  SRCS_C += src/drivers/block/raid.c
endif
ifeq ($(CONFIG_NET_TCP_IP),y)
  SRCS_C += src/drivers/net/net.c
endif
ifeq ($(CONFIG_NET_E1000),y)
  SRCS_C += src/drivers/net/e1000.c
endif
ifeq ($(CONFIG_NET_RTL8139),y)
  SRCS_C += src/drivers/net/rtl8139.c
endif
ifeq ($(CONFIG_NET_RTL8169),y)
  SRCS_C += src/drivers/net/rtl8169.c
endif
ifeq ($(CONFIG_NET_VIRTIO),y)
  SRCS_C += src/drivers/net/virtio_net.c
endif
ifeq ($(CONFIG_NET_WIFI),y)
  SRCS_C += src/drivers/net/wifi.c
endif
ifeq ($(CONFIG_AUDIO_AC97),y)
  SRCS_C += src/drivers/audio/ac97.c
endif
ifeq ($(CONFIG_AUDIO_HDA),y)
  SRCS_C += src/drivers/audio/hda.c
endif
ifeq ($(CONFIG_USB_XHCI),y)
  SRCS_C += src/drivers/usb/xhci.c
  SRCS_C += src/drivers/usb/xhci_hid.c
  SRCS_C += src/drivers/usb/usb_hid.cpp
endif
ifeq ($(CONFIG_INPUT_PS2),y)
  SRCS_C += src/drivers/input/ps2.c
endif
ifeq ($(CONFIG_TTY),y)
  SRCS_C += src/drivers/char/tty.c
endif
ifeq ($(CONFIG_VIDEO_HDMI),y)
  SRCS_C += src/drivers/video/hdmi.c
endif

# Filesystems
ifeq ($(CONFIG_FS_FAT32_READ),y)
  SRCS_C += src/fs/fat32.c
endif
ifeq ($(CONFIG_FS_EXFAT_READ),y)
  SRCS_C += src/fs/exfat.c
endif
ifneq ($(filter y,$(CONFIG_FS_EXT2_READ)$(CONFIG_FS_EXT3_READ)$(CONFIG_FS_EXT4_READ)),)
  SRCS_C += src/fs/extfs.c
endif

# Tests
ifeq ($(CONFIG_STORAGE_TESTS),y)
  SRCS_C += src/tests/storage_tests.c
endif

SRCS_CPP := $(filter %.cpp, $(SRCS_C))
SRCS_C := $(filter %.c, $(SRCS_C))
OBJS = build/switch.o build/cpu/irq.o build/fonts/terminal_psf.o \
       $(patsubst src/%.c, build/%.o, $(SRCS_C)) \
       $(patsubst src/%.cpp, build/%.o, $(SRCS_CPP))

INIT_SRC = src/programs/init_sample.c
INIT_ELF = build/programs/init.elf


all: check_config build/kernel.elf

compress: check_config build/kernel.elf build/kernel.final.elf

build/kernel.bin: build/kernel.elf
	@printf "  OBJCOPY $@\n"
	@objcopy -O binary $< $@

build/kernel.bin.gz: build/kernel.bin
	@printf "  GZIP    $@\n"
	@gzip -9 -c $< > $@

build/decompress_stub.elf: src/boot/decompress_entry.asm src/boot/decompress_main.c src/lib/inflate.c src/boot/decompress.ld
	@mkdir -p build/boot
	@printf "  NASM    src/boot/decompress_entry.asm\n"
	@nasm -f elf64 src/boot/decompress_entry.asm -o build/boot/decompress_entry.o
	@printf "  GCC     src/boot/decompress_main.c\n"
	@gcc $(CFLAGS) -c src/boot/decompress_main.c -o build/boot/decompress_main.o
	@printf "  GCC     src/lib/inflate.c (decompressor)\n"
	@gcc $(CFLAGS) -c src/lib/inflate.c -o build/boot/inflate_decomp.o
	@printf "  LD      $@\n"
	@ld -n -T src/boot/decompress.ld -z max-page-size=0x1000 --no-warn-rwx-segments -z noexecstack \
		build/boot/decompress_entry.o build/boot/decompress_main.o build/boot/inflate_decomp.o -o $@

build/kernel.final.elf: build/kernel.bin.gz build/decompress_stub.elf
	@printf "  PACK    $@\n"
	@python3 tools/pack_kernel.py build/decompress_stub.elf build/kernel.bin.gz $@

check_config:
	@if [ ! -f .config ]; then \
		echo "No .config file found!"; \
		read -p "Would you like to proceed with 'defconfig'? [y/N] " ans; \
		if [ "$$ans" = "y" ] || [ "$$ans" = "Y" ]; then \
			$(MAKE) defconfig; \
		else \
			echo "Configuration required. Exiting."; \
			exit 1; \
		fi; \
	fi

build/kernel.elf: $(OBJS)
	@printf "  LD      $@\n"
	@$(LD) $(LDFLAGS) -o $@ $(OBJS)

build/switch.o: src/boot/switch.asm
	@mkdir -p build
	@printf "  NASM    $<\n"
	@$(AS) $(ASFLAGS) $< -o $@

build/cpu/irq.o: src/cpu/irq.asm
	@mkdir -p build/cpu
	@printf "  NASM    $<\n"
	@$(AS) $(ASFLAGS) $< -o $@

build/fonts/terminal_psf.o: src/fonts/terminal.psf
	@mkdir -p build/fonts
	@printf "  FONT    $<\n"
	@ld -r -b binary -o $@ $<

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	@printf "  GCC     $<\n"
	@$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	@printf "  GXX     $<\n"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(INIT_ELF): $(INIT_SRC)
	@mkdir -p build/programs
	@printf "  USER    $<\n"
	@$(CC) -m64 -static -nostdlib -e _start -Ttext 0x400000 $< -o $@


.PHONY: all check_config help menuconfig xconfig defconfig tinyconfig allnoconfig allyesconfig allmodconfig uefi-bundle iso isos making run clean distclean install
help:
	@printf "Peanut kernel make targets:\n"
	@printf "  make all              Build the kernel (build/kernel.elf)\n"
	@printf "  make compress         Build compressed self-decompressing kernel\n"
	@printf "  make menuconfig       Launch Kconfig menuconfig\n"
	@printf "  make xconfig          Launch Kconfig Qt config\n"
	@printf "  make defconfig        Apply default configuration\n"
	@printf "  make tinyconfig       Minimal configuration\n"
	@printf "  make allnoconfig      All options disabled\n"
	@printf "  make allyesconfig     All options enabled\n"
	@printf "  make allmodconfig     All options modular\n"
	@printf "  make iso              Build ISO for GRUB (kernel only)\n"
	@printf "  make isos             Build all ISO/EFI bundle artifacts\n"
	@printf "  make disk.img         Build partitioned FAT32 disk with init\n"
	@printf "  make disk-exfat.img   Build exFAT disk with init\n"
	@printf "  make disk-ext4.img    Build ext4 disk with init\n"
	@printf "  make making           Build ISO + all supported disks\n"
	@printf "  make uefi-bundle      Copy kernel.elf into build/efi/BOOT/PEANUT.KRN\n"
	@printf "  make run              Run qemu with iso + disk.img\n"
	@printf "  make run-serial       Run qemu with serial console on stdio\n"
	@printf "  make run-nvme         Run qemu with NVMe controller + nvme-disk.img\n"
	@printf "  make run-net          Run qemu with e1000 NIC + serial console\n"
	@printf "  make run-uefi         Run qemu with UEFI firmware\n"
	@printf "  make nvme-disk.img    Create empty 64M NVMe disk image\n"
	@printf "  make install          Install kernel to /boot/peanut.elf\n"
	@printf "  make clean            Remove build artifacts + .config + disk images\n"
	@printf "  make distclean        clean + remove kernel config\n"

# --- Configuration ---

menuconfig:
	@echo "menuconfig"
	@./scripts/kconfig/mconf Kconfig

xconfig:
	@echo "xconfig"
	@./scripts/kconfig/qconf Kconfig

defconfig:
	@echo "alldefconfig Kconfig"
	@./scripts/kconfig/conf --alldefconfig Kconfig
	@mkdir -p include/generated
	@./scripts/kconfig/conf --silentoldconfig Kconfig

tinyconfig:
	@echo "tinyconfig Kconfig"
	@./scripts/kconfig/conf --tinyconfig Kconfig

allnoconfig:
	@echo "allnoconfig Kconfig"
	@./scripts/kconfig/conf --allnoconfig Kconfig

allyesconfig:
	@echo "allyesconfig Kconfig"
	@./scripts/kconfig/conf --allyesconfig Kconfig

allmodconfig:
	@echo "allmodconfig Kconfig"
	@./scripts/kconfig/conf --allmodconfig Kconfig

uefi-bundle: build/kernel.elf
	@printf "UEFI: Multiboot2 kernel.elf is built. For firmware-native PE/COFF use an external loader or gnu-efi; see docs/KERNEL_ABI.md.\n"
	@mkdir -p build/efi/BOOT
	@cp build/kernel.elf build/efi/BOOT/PEANUT.KRN
	@printf "Copied kernel to build/efi/BOOT/PEANUT.KRN (ELF64 payload for a compatible loader).\n"

# --- Execution ---

iso: build/kernel.elf
	@mkdir -p build/isofiles/boot/grub
	@cp build/kernel.elf build/isofiles/boot/kernel.elf
	@echo 'set timeout=0' > build/isofiles/boot/grub/grub.cfg
	@echo 'set default=0' >> build/isofiles/boot/grub/grub.cfg
	@echo 'menuentry "Peanut OS" {' >> build/isofiles/boot/grub/grub.cfg
	@echo '    multiboot2 /boot/kernel.elf' >> build/isofiles/boot/grub/grub.cfg
	@echo '    boot' >> build/isofiles/boot/grub/grub.cfg
	@echo '}' >> build/isofiles/boot/grub/grub.cfg
	@grub-mkrescue -o build/peanut.iso build/isofiles

install: build/kernel.elf
	@echo "Installing Peanut kernel..."
	@if [ -n "$(DESTDIR)" ]; then \
		mkdir -p $(DESTDIR)/boot; \
		install -m 644 build/kernel.elf $(DESTDIR)/boot/peanut.elf; \
		echo "Installed to $(DESTDIR)/boot/peanut.elf"; \
	else \
		install -m 644 build/kernel.elf /boot/peanut.elf 2>/dev/null || \
		(sudo install -m 644 build/kernel.elf /boot/peanut.elf); \
		echo "Installed to /boot/peanut.elf"; \
	fi

isos: iso uefi-bundle

making: isos disk.img disk-exfat.img disk-ext4.img

disk.img: $(INIT_ELF)
	@printf "  DISK     Generating partitioned disk.img\n"
	rm -f disk.img
	dd if=/dev/zero of=disk.img bs=1M count=64
	echo "label: dos" | sfdisk disk.img
	echo "2048,,c,*" | sfdisk disk.img
	mkfs.vfat -F 32 --offset=2048 disk.img
	mmd -i disk.img@@1M ::/SBIN
	mmd -i disk.img@@1M ::/BOOT
	mmd -i disk.img@@1M ::/BIN
	mmd -i disk.img@@1M ::/USR
	mmd -i disk.img@@1M ::/LIB
	mmd -i disk.img@@1M ::/ETC
	mmd -i disk.img@@1M ::/DEV
	mcopy -i disk.img@@1M $(INIT_ELF) ::/SBIN/INIT
	mcopy -i disk.img@@1M $(INIT_ELF) ::/INIT
	@printf 'root:x:0:0:root:/root:/sbin/init\nuser:x:1000:1000:Peanut User:/home/user:/bin/sh\n' > build/programs/passwd
	@printf 'devtmpfs /dev devtmpfs rw 0 0\nproc /proc proc ro 0 0\nsysfs /sys sysfs ro 0 0\n' > build/programs/fstab
	mcopy -i disk.img@@1M build/programs/passwd ::/ETC/PASSWD
	mcopy -i disk.img@@1M build/programs/fstab ::/ETC/FSTAB

run: iso disk.img
	qemu-system-x86_64 -boot d -cdrom build/peanut.iso \
	-drive file=disk.img,format=raw,if=ide,index=0,media=disk \
	-device qemu-xhci \
	-device usb-kbd \
	-device usb-mouse

run-serial: iso disk.img
	qemu-system-x86_64 -boot d -cdrom build/peanut.iso \
	-drive file=disk.img,format=raw,if=ide,index=0,media=disk \
	-device qemu-xhci \
	-device usb-kbd \
	-device usb-mouse \
	-serial stdio

run-nvme: iso disk.img
	qemu-system-x86_64 -boot d -cdrom build/peanut.iso \
	-drive file=disk.img,format=raw,if=ide,index=0,media=disk \
	-drive file=nvme-disk.img,format=raw,if=none,id=nvme0 \
	-device nvme,serial=deadbeef,drive=nvme0 \
	-device qemu-xhci \
	-device usb-kbd \
	-device usb-mouse

run-net: iso disk.img
	qemu-system-x86_64 -boot d -cdrom build/peanut.iso \
	-drive file=disk.img,format=raw,if=ide,index=0,media=disk \
	-device e1000,netdev=net0 \
	-netdev user,id=net0 \
	-device qemu-xhci \
	-device usb-kbd \
	-device usb-mouse \
	-serial stdio

run-uefi: iso uefi-bundle
	qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd -boot d -cdrom build/peanut.iso \
	-drive file=disk.img,format=raw,if=ide,index=0,media=disk \
	-device qemu-xhci \
	-device usb-kbd \
	-device usb-mouse

nvme-disk.img:
	@printf "  DISK     Generating NVMe disk image\n"
	dd if=/dev/zero of=nvme-disk.img bs=1M count=64
	@printf 'label: dos\n2048,,c,*\n' | sfdisk nvme-disk.img
	mkfs.vfat -F 32 --offset=2048 nvme-disk.img

disk-exfat.img: $(INIT_ELF)
	@printf "  DISK     Generating exFAT disk-exfat.img (no partition table)\n"
	rm -f disk-exfat.img
	dd if=/dev/zero of=disk-exfat.img bs=1M count=64
	@command -v mkfs.exfat >/dev/null 2>&1 || (echo "mkfs.exfat not found (install exfatprogs)"; exit 1)
	mkfs.exfat -n PEANUT disk-exfat.img
	python3 tools/populate_exfat.py disk-exfat.img $(INIT_ELF)

disk-ext4.img: $(INIT_ELF)
	@printf "  DISK     Generating ext4 disk-ext4.img (no partition table)\n"
	rm -f disk-ext4.img
	dd if=/dev/zero of=disk-ext4.img bs=1M count=64
	@command -v mkfs.ext4 >/dev/null 2>&1 || (echo "mkfs.ext4 not found (install e2fsprogs)"; exit 1)
	mkfs.ext4 -F -O ^64bit disk-ext4.img
	@command -v debugfs >/dev/null 2>&1 || (echo "debugfs not found (install e2fsprogs)"; exit 1)
	debugfs -w -R "mkdir /SBIN" disk-ext4.img
	debugfs -w -R "mkdir /BOOT" disk-ext4.img
	debugfs -w -R "mkdir /BIN" disk-ext4.img
	debugfs -w -R "mkdir /USR" disk-ext4.img
	debugfs -w -R "mkdir /LIB" disk-ext4.img
	debugfs -w -R "mkdir /ETC" disk-ext4.img
	debugfs -w -R "mkdir /DEV" disk-ext4.img
	debugfs -w -R "write $(INIT_ELF) /SBIN/INIT" disk-ext4.img
	debugfs -w -R "write $(INIT_ELF) /INIT" disk-ext4.img
	@printf 'root:x:0:0:root:/root:/sbin/init\nuser:x:1000:1000:Peanut User:/home/user:/bin/sh\n' > build/programs/passwd
	@printf 'devtmpfs /dev devtmpfs rw 0 0\nproc /proc proc ro 0 0\nsysfs /sys sysfs ro 0 0\n' > build/programs/fstab
	debugfs -w -R "write build/programs/passwd /ETC/PASSWD" disk-ext4.img
	debugfs -w -R "write build/programs/fstab /ETC/FSTAB" disk-ext4.img

clean:
	rm -rf build/ $(KCONFIG_HEADER) .config .config.old disk.img disk-exfat.img disk-ext4.img nvme-disk.img
	rm -rf include/generated include/config/auto.conf include/config/auto.conf.cmd include/config/tristate.conf
	rm -f .tmpconfig .tmpconfig.h .tmpconfig_tristate

distclean: clean
	rm -f .config.old
