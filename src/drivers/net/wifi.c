#include "drivers/net/wifi.h"
#include "drivers/bus/pci.h"
#include "freelib/kstdio.h"

typedef struct {
    uint16_t vendor;
    uint16_t device;
    const char* name;
} WifiPciId;

static const WifiPciId wifi_ids[] = {
    { 0x8086, 0x0084, "Intel Centrino Advanced-N 6200" },
    { 0x8086, 0x0085, "Intel Centrino Advanced-N 6200" },
    { 0x8086, 0x0087, "Intel Centrino Ultimate-N 6300" },
    { 0x8086, 0x0089, "Intel Centrino Advanced-N 6230" },
    { 0x8086, 0x008B, "Intel Centrino Advanced-N 6230" },
    { 0x8086, 0x0090, "Intel Centrino Wireless-N 1030" },
    { 0x8086, 0x0091, "Intel Centrino Wireless-N 1030" },
    { 0x8086, 0x4222, "Intel PRO/Wireless 3945ABG"},
    { 0x8086, 0x4229, "Intel PRO/Wireless 4965AGN"},
    { 0x8086, 0x4230, "Intel PRO/Wireless 4965AGN"},
    { 0x8086, 0x4232, "Intel WiFi Link 5100"},
    { 0x8086, 0x4237, "Intel WiFi Link 5100"},
    { 0x8086, 0x4238, "Intel WiFi Link 5300"},
    { 0x8086, 0x4239, "Intel WiFi Link 5300"},
    { 0x8086, 0x423B, "Intel WiFi Link 5000"},
    { 0x8086, 0x423C, "Intel WiFi Link 5000"},
    { 0x8086, 0x423D, "Intel WiFi Link 5000"},
    { 0x10EC, 0x8176, "Realtek RTL8188CE" },
    { 0x10EC, 0x8179, "Realtek RTL8188EE" },
    { 0x10EC, 0x818B, "Realtek RTL8192EE" },
    { 0x10EC, 0x8192, "Realtek RTL8192CE" },
    { 0x10EC, 0x8193, "Realtek RTL8192DE" },
    { 0x1814, 0x0601, "Ralink RT3090" },
    { 0x1814, 0x0781, "Ralink RT3592" },
    { 0x1814, 0x5360, "Ralink RT5360" },
    { 0x1814, 0x5390, "Ralink RT5390" },
    { 0x168C, 0x0013, "Atheros AR5212" },
    { 0x168C, 0x001A, "Atheros AR5008" },
    { 0x168C, 0x001B, "Atheros AR5008" },
    { 0x168C, 0x0023, "Atheros AR5416" },
    { 0x168C, 0x0024, "Atheros AR5418" },
    { 0x168C, 0x002A, "Atheros AR9285" },
    { 0x168C, 0x002B, "Atheros AR9287" },
    { 0x168C, 0x002C, "Atheros AR9485" },
    { 0x168C, 0x002D, "Atheros AR9462" },
    { 0x168C, 0x0030, "Atheros AR9380" },
    { 0x168C, 0x0032, "Atheros AR9485" },
    { 0x168C, 0x0033, "Atheros AR9580" },
    { 0x168C, 0x0034, "Atheros AR9462" },
    { 0x168C, 0x0036, "Atheros AR9565" },
    { 0x14E4, 0x4311, "Broadcom BCM4311" },
    { 0x14E4, 0x4312, "Broadcom BCM4312" },
    { 0x14E4, 0x4315, "Broadcom BCM4313" },
    { 0x14E4, 0x4328, "Broadcom BCM4328" },
    { 0x14E4, 0x4329, "Broadcom BCM4329" },
    { 0x14E4, 0x432B, "Broadcom BCM43224" },
    { 0x14E4, 0x4331, "Broadcom BCM4331" },
    { 0x14E4, 0x4353, "Broadcom BCM4352" },
    { 0, 0, NULL }
};

void wifi_init(void) {
    int found = 0;
    for (const WifiPciId* id = wifi_ids; id->name; id++) {
        PciAddress addr;
        if (pci_find_device(id->vendor, id->device, &addr) == 0) {
            kprint("WiFi: found ");
            kprint(id->name);
            kprint(" (");
            kprint_hex((uint32_t)id->vendor << 16 | id->device);
            kprint(")\n");
            kprint("  WiFi: adapter detected; data path not connected\n");
            found = 1;
        }
    }
    if (!found)
        kprint("WiFi: no wireless adapter found\n");
}
