#include "drivers/video/hdmi.h"
#include "drivers/bus/pci.h"
#include "freelib/kstdio.h"

static int display_found;
static int audio_found;
static char status[128];

static uint32_t strlen_local(const char* s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static void append_char(char* out, uint32_t cap, uint32_t* pos, char c) {
    if (*pos + 1 >= cap)
        return;
    out[*pos] = c;
    (*pos)++;
    out[*pos] = '\0';
}

static void append_hex16(char* out, uint32_t cap, uint32_t* pos, uint16_t v) {
    const char* hex = "0123456789ABCDEF";
    for (int shift = 12; shift >= 0; shift -= 4)
        append_char(out, cap, pos, hex[(v >> shift) & 0xF]);
}

static void append_text(char* out, uint32_t cap, uint32_t* pos, const char* s) {
    for (uint32_t i = 0; s[i]; i++)
        append_char(out, cap, pos, s[i]);
}

static void set_status(PciAddress display, uint32_t display_id, int have_display,
                       PciAddress audio, uint32_t audio_id, int have_audio) {
    uint32_t p = 0;
    status[0] = '\0';
    if (have_display) {
        append_text(status, sizeof(status), &p, "display=");
        append_hex16(status, sizeof(status), &p, (uint16_t)(display_id & 0xFFFF));
        append_char(status, sizeof(status), &p, ':');
        append_hex16(status, sizeof(status), &p, (uint16_t)(display_id >> 16));
        append_text(status, sizeof(status), &p, " pci=");
        append_hex16(status, sizeof(status), &p, display.bus);
        append_char(status, sizeof(status), &p, ':');
        append_hex16(status, sizeof(status), &p, display.slot);
        append_char(status, sizeof(status), &p, '.');
        append_hex16(status, sizeof(status), &p, display.function);
    } else {
        append_text(status, sizeof(status), &p, "display=none");
    }
    if (have_audio) {
        append_text(status, sizeof(status), &p, " hdmi-audio=");
        append_hex16(status, sizeof(status), &p, (uint16_t)(audio_id & 0xFFFF));
        append_char(status, sizeof(status), &p, ':');
        append_hex16(status, sizeof(status), &p, (uint16_t)(audio_id >> 16));
        append_text(status, sizeof(status), &p, " pci=");
        append_hex16(status, sizeof(status), &p, audio.bus);
        append_char(status, sizeof(status), &p, ':');
        append_hex16(status, sizeof(status), &p, audio.slot);
        append_char(status, sizeof(status), &p, '.');
        append_hex16(status, sizeof(status), &p, audio.function);
    }
    append_char(status, sizeof(status), &p, '\n');
    (void)strlen_local;
}

void hdmi_init(void) {
    PciAddress display = {0};
    PciAddress audio = {0};
    uint32_t display_id = 0;
    uint32_t audio_id = 0;
    display_found = pci_find_class_any_prog_if(0x03, 0x00, &display) == 0 ||
                    pci_find_class_any_prog_if(0x03, 0x80, &display) == 0;
    if (display_found)
        display_id = pci_read32(display, 0x00);
    audio_found = pci_find_class_any_prog_if(0x04, 0x03, &audio) == 0;
    if (audio_found)
        audio_id = pci_read32(audio, 0x00);
    set_status(display, display_id, display_found, audio, audio_id, audio_found);
    if (display_found) {
        kprint("HDMI/display: PCI display controller found\n");
    }
    if (audio_found) {
        kprint("HDMI/display: HD-audio function found\n");
    }
}

int hdmi_present(void) {
    return display_found;
}

const char* hdmi_status(void) {
    return status[0] ? status : "display=none\n";
}
