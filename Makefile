CC = gcc
AS = nasm
LD = ld

KCONFIG_HEADER = include/generated/autoconf.h

CFLAGS = -ffreestanding -mcmodel=kernel -mno-red-zone -m64 -mno-sse -mno-sse2 \
         -fno-pic -fno-pie -fno-stack-protector -fcf-protection=none \
         -Iinclude -Iinclude/generated -Isrc
ASFLAGS = -f elf64
LDFLAGS = -n -T linker.ld -z max-page-size=0x1000 --no-warn-rwx-segments -z noexecstack

ALL_C = $(shell find src -name '*.c' -print | sort)
SRCS_C = $(filter-out src/programs/init_sample.c, $(ALL_C))

OBJS = build/switch.o build/cpu/irq.o build/fonts/terminal_psf.o $(patsubst src/%.c, build/%.o, $(SRCS_C))

INIT_SRC = src/programs/init_sample.c
INIT_ELF = build/programs/init.elf


all: check_config build/kernel.elf

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

$(INIT_ELF): $(INIT_SRC)
	@mkdir -p build/programs
	@printf "  USER    $<\n"
	@$(CC) -m64 -static -nostdlib -e _start -Ttext 0x400000 $< -o $@


.PHONY: all check_config help menuconfig defconfig uefi-bundle iso isos making run clean distclean
help:
	@printf "Peanut-kernel make targets:\n"
	@printf "  make all              Build the kernel (build/kernel.elf)\n"
	@printf "  make defconfig       Generate include/generated/autoconf.h via alldefconfig\n"
	@printf "  make menuconfig      Launch Kconfig menuconfig\n"
	@printf "  make iso             Build ISO for GRUB (kernel only)\n"
	@printf "  make isos            Build all ISO/EFI bundle artifacts\n"
	@printf "  make disk.img         Build partitioned FAT32 disk with init\n"
	@printf "  make disk-exfat.img  Build exFAT disk with init\n"
	@printf "  make disk-ext4.img   Build ext4 disk with init\n"
	@printf "  make making          Build ISO + all supported disks\n"
	@printf "  make uefi-bundle     Copy kernel.elf into build/efi/BOOT/PEANUT.KRN\n"
	@printf "  make run             Run qemu with iso + disk.img\n"
	@printf "  make clean           Remove build artifacts + .config + disk images\n"
	@printf "  make distclean       clean + remove kernel config\n"

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
	-device usb-kbd

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
	rm -rf build/ $(KCONFIG_HEADER) .config disk.img disk-exfat.img disk-ext4.img

distclean: clean
	rm -f .config.old
