#include "freelib/kstdio.h"
#include "freelib/kstdint.h"
#include "freelib/kalloc.h"
#include "freelib/kpanic.h"
#include "storage.h"
#include "programs/elf.h"
#include "config.h"
#include "cpu/idt.h"
#include "cpu/syscall.h"
#include "cpu/pic.h"
#include "cpu/pit.h"
#include "cpu/sched.h"
#include "fs/vfs.h"
#include "fs/initramfs.h"
#include "mm/vm.h"
#include "freelib/slab.h"
#include "users.h"
#include "drivers/video/fb.h"

#ifdef CONFIG_INPUT_PS2
#include "drivers/input/ps2.h"
#endif

#ifdef CONFIG_VIDEO_HDMI
#include "drivers/video/hdmi.h"
#endif

#ifdef CONFIG_USB_XHCI
#include "drivers/usb/xhci.h"
int usb_kbd_init(void);
#endif

#ifdef CONFIG_STORAGE_CDROM
#include "drivers/block/cdrom.h"
#endif

#ifdef CONFIG_STORAGE_FLOPPY
#include "drivers/block/floppy.h"
#endif

#ifdef CONFIG_NET_TCP_IP
#include "drivers/net/net.h"
#ifdef CONFIG_NET_E1000
#include "drivers/net/e1000.h"
#endif
#ifdef CONFIG_NET_RTL8139
#include "drivers/net/rtl8139.h"
#endif
#ifdef CONFIG_NET_RTL8169
#include "drivers/net/rtl8169.h"
#endif
#ifdef CONFIG_NET_VIRTIO
#include "drivers/net/virtio_net.h"
#endif
#endif

#ifdef CONFIG_TTY
#include "drivers/char/tty.h"
#endif

#ifdef CONFIG_NET_WIFI
#include "drivers/net/wifi.h"
#endif

#ifdef CONFIG_AUDIO_AC97
#include "drivers/audio/ac97.h"
#endif

#ifdef CONFIG_AUDIO_HDA
#include "drivers/audio/hda.h"
#endif

#ifdef CONFIG_STORAGE_NVME
#include "drivers/block/nvme.h"
#endif

#ifdef CONFIG_BLOCK_RAID
#include "drivers/block/raid.h"
#endif

void gdt_init_for_user();
void tss_init();
void irq_timer(void);
void kmain(uint64_t multiboot_info, uint32_t kaslr_offset) {
    (void)kaslr_offset;
    if (fb_init_direct() != 0)
        fb_init_from_multiboot(multiboot_info);
    kclear();
    kprint_timed("Peanut Kernel booting...\n");

#ifdef CONFIG_MANTAINER_MENTAL_STABILITY
    kprint_timed("  [Maintainer mental stability engaged — pausing 10 s]\n");
    {
        volatile uint8_t pointless_stack[8192];
        for (uint32_t di = 0; di < 8192; di++) pointless_stack[di] = (uint8_t)di;
        for (volatile uint32_t di = 0; di < 500000000; di++) {
            __asm__ volatile("pause");
            volatile uint8_t sink_ = pointless_stack[di & 8191];
            (void)sink_;
        }
    }
    kprint_timed("  [Stability pause complete — resuming boot]\n");
#endif

    vm_init();
    slab_init();
    kalloc_init();
    gdt_init_for_user();
    tss_init();
    idt_init();
    syscall_init();

    pic_init();
    pit_init_hz(100);
    sched_init();
    idt_set_handler(32, irq_timer);

    vfs_init();
    vfs_mount_devtmpfs();
    initramfs_init();
    users_init();

#ifdef CONFIG_INPUT_PS2
    ps2_init();
#endif

#ifdef CONFIG_USB_XHCI
    xhci_init();
    usb_kbd_init();
#endif

#ifdef CONFIG_VIDEO_HDMI
    hdmi_init();
#endif

#ifdef CONFIG_STORAGE_CDROM
    cdrom_init();
#endif

#ifdef CONFIG_STORAGE_FLOPPY
    floppy_init();
#endif

#ifdef CONFIG_NET_TCP_IP
    kprint_timed("Network stack enabled\n");
    net_init();
#ifdef CONFIG_NET_E1000
    e1000_init();
#endif
#ifdef CONFIG_NET_RTL8139
    rtl8139_init();
#endif
#ifdef CONFIG_NET_RTL8169
    rtl8169_init();
#endif
#ifdef CONFIG_NET_VIRTIO
    virtio_net_init();
#endif
#endif

#ifdef CONFIG_TTY
    tty_init();
#endif

#ifdef CONFIG_NET_WIFI
    wifi_init();
#endif

#ifdef CONFIG_STORAGE_NVME
    nvme_init();
#endif

#ifdef CONFIG_BLOCK_RAID
    raid_init();
#endif

#ifdef CONFIG_AUDIO_AC97
    ac97_init();
#endif

#ifdef CONFIG_AUDIO_HDA
    hda_init();
#endif

    storage_init_required();
    PeanutVolume* root = storage_get_root_volume();

    if (!root) {
        kpanic("[Peanut kernel - panic - No bootable FAT32 volume found!]");
    }

    kprint_timed("Loading init process...\n");

    if (elf_load_and_run(root, CONFIG_INIT_PATH) == 0) {
        goto reached_init;
    }

    kprint_timed("init not found, trying /INIT...\n");
    if (elf_load_and_run(root, "/INIT") == 0) {
        goto reached_init;
    }

    if (elf_load_and_run(root, "/sbin/init") == 0) {
        goto reached_init;
    }

    kpanic("[Peanut kernel - panic - No working init found!]");

reached_init:
    kpanic("[Peanut kernel - panic - Attempted to kill init!]");
}
