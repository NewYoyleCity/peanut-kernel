/*
 * ACPI driver — Advanced Configuration and Power Interface
 *
 * Provides:
 *   - RSDP (Root System Description Pointer) discovery
 *   - RSDT/XSDT table iteration
 *   - FADT (Fixed ACPI Description Table) parsing
 *   - DSDT (Differentiated System Description Table) discovery
 *   - ACPI poweroff (via FADT PM1a_CNT register block)
 *   - ACPI reboot (via FADT RESET_REG)
 *   - Minimal SMI/SCI awareness
 *
 * The ACPI spec requires the RSDP to be searched for in:
 *   1. The first 1 KB of the EBDA (Extended BIOS Data Area)
 *   2. The BIOS ROM from 0x000E0000 to 0x000FFFFF
 *
 * This implementation searches both locations, walks the SDT chain,
 * and extracts the FADT for power management.
 */

#include "config.h"
#include "freelib/kstdint.h"
#include "freelib/kstdio.h"
#include "drivers/bus/io.h"

/* RSDP (Root System Description Pointer) revision 1 (ACPI 1.0) */
typedef struct {
    char     signature[8];      /* "RSD PTR " */
    uint8_t  checksum;          /* Checksum of the first 20 bytes */
    char     oem_id[6];         /* OEM identifier */
    uint8_t  revision;          /* 0 for ACPI 1.0, 2 for ACPI 3.0+ */
    uint32_t rsdt_address;      /* 32-bit physical address of RSDT */
} __attribute__((packed)) acpi_rsdp_t;

/* RSDP revision 2+ extension (ACPI 2.0+) */
typedef struct {
    acpi_rsdp_t rsdp;           /* Base RSDP structure (20 bytes) */
    uint32_t    length;         /* Total length of RSDP including extensions */
    uint64_t    xsdt_address;   /* 64-bit physical address of XSDT */
    uint8_t     ext_checksum;   /* Checksum of the entire RSDP */
    uint8_t     reserved[3];
} __attribute__((packed)) acpi_rsdp2_t;

/* SDT (System Description Table) header — common to all ACPI tables */
typedef struct {
    char     signature[4];      /* Table signature, e.g. "FACP", "APIC", "DSDT" */
    uint32_t length;            /* Total length of the table including header */
    uint8_t  revision;          /* Revision */
    uint8_t  checksum;          /* Checksum of the entire table */
    char     oem_id[6];         /* OEM ID */
    char     oem_table_id[8];   /* OEM Table ID */
    uint32_t oem_revision;      /* OEM Revision */
    uint32_t creator_id;        /* Creator ID */
    uint32_t creator_revision;  /* Creator Revision */
} __attribute__((packed)) acpi_sdt_t;

/* RSDT — Root System Description Table (32-bit pointers) */
typedef struct {
    acpi_sdt_t header;          /* "RSDT" signature */
    uint32_t   entries[];       /* Array of 32-bit physical addresses */
} __attribute__((packed)) acpi_rsdt_t;

/* XSDT — Extended System Description Table (64-bit pointers) */
typedef struct {
    acpi_sdt_t header;          /* "XSDT" signature */
    uint64_t   entries[];       /* Array of 64-bit physical addresses */
} __attribute__((packed)) acpi_xsdt_t;

/* FADT — Fixed ACPI Description Table */
typedef struct {
    acpi_sdt_t  header;         /* "FACP" signature */
    uint32_t    firmware_ctrl;  /* Physical address of FACS */
    uint32_t    dsdt;           /* Physical address of DSDT */
    uint8_t     reserved1;      /* (field used for interrupt model in ACPI 1.0) */
    uint8_t     preferred_pm_profile;
    uint16_t    sci_int;        /* System Control Interrupt */
    uint32_t    smi_cmd;        /* SMI command port */
    uint8_t     acpi_enable;    /* Value to write to SMI_CMD to enable ACPI */
    uint8_t     acpi_disable;   /* Value to write to SMI_CMD to disable ACPI */
    uint8_t     s4bios_req;     /* Value to enter S4BIOS state */
    uint8_t     pstate_cnt;
    uint32_t    pm1a_evt_blk;   /* PM1a event register block base */
    uint32_t    pm1b_evt_blk;   /* PM1b event register block base */
    uint32_t    pm1a_cnt_blk;   /* PM1a control register block base */
    uint32_t    pm1b_cnt_blk;   /* PM1b control register block base */
    uint32_t    pm2_cnt_blk;    /* PM2 control register block base */
    uint32_t    pm_tmr_blk;     /* PM timer register block base */
    uint32_t    gpe0_blk;       /* General Purpose Event 0 block base */
    uint32_t    gpe1_blk;       /* GPE1 block base */
    uint8_t     pm1_evt_len;    /* Length of PM1 event block */
    uint8_t     pm1_cnt_len;    /* Length of PM1 control block */
    uint8_t     pm2_cnt_len;    /* Length of PM2 control block */
    uint8_t     pm_tmr_len;     /* Length of PM timer block */
    uint8_t     gpe0_blk_len;   /* Length of GPE0 block */
    uint8_t     gpe1_blk_len;   /* Length of GPE1 block */
    uint8_t     gpe1_base;      /* Offset in GPE number space where GPE1 starts */
    uint8_t     cst_cnt;
    uint16_t    p_lvl2_lat;     /* C2 latency */
    uint16_t    p_lvl3_lat;     /* C3 latency */
    uint16_t    flush_size;     /* Size of flush cache line */
    uint16_t    flush_stride;   /* Stride of flush cache lines */
    uint8_t     duty_offset;    /* Duty cycle offset */
    uint8_t     duty_width;     /* Duty cycle width */
    uint8_t     day_alarm;      /* Day alarm field offset in CMOS RAM */
    uint8_t     mon_alarm;      /* Month alarm offset */
    uint8_t     century;        /* Century field offset */
    uint16_t    iapc_boot_arch; /* IA-PC boot architecture flags */
    uint8_t     reserved2;      /* Must be 0 */
    uint32_t    flags;          /* Fixed feature flags */
    /* Fields added in ACPI 2.0 start here: */
    uint8_t     reset_reg[12];  /* Generic Address Structure for reset */
    uint8_t     reset_value;    /* Value to write to RESET_REG to reset */
    uint16_t    arm_boot_arch;  /* ARM boot architecture flags (ACPI 5.1+) */
    uint8_t     minor_revision; /* Minor revision (ACPI 5.1+) */
    uint64_t    x_firmware_ctrl;/* 64-bit FACS address */
    uint64_t    x_dsdt;         /* 64-bit DSDT address */
    uint8_t     x_pm1a_evt_blk[12]; /* Extended PM1a event block */
    uint8_t     x_pm1b_evt_blk[12]; /* Extended PM1b event block */
    uint8_t     x_pm1a_cnt_blk[12]; /* Extended PM1a control block */
    uint8_t     x_pm1b_cnt_blk[12]; /* Extended PM1b control block */
    uint8_t     x_pm2_cnt_blk[12];  /* Extended PM2 control block */
    uint8_t     x_pm_tmr_blk[12];   /* Extended PM timer block */
    uint8_t     x_gpe0_blk[12];     /* Extended GPE0 block */
    uint8_t     x_gpe1_blk[12];     /* Extended GPE1 block */
} __attribute__((packed)) acpi_fadt_t;

/* Generic Address Structure (GAS) used in ACPI 2.0+ */
typedef struct {
    uint8_t  address_space_id;   /* 0=system mem, 1=system IO */
    uint8_t  register_bit_width;
    uint8_t  register_bit_offset;
    uint8_t  reserved;
    uint64_t address;
} __attribute__((packed)) acpi_gas_t;

/* Forward declarations */
static acpi_fadt_t *fadt = NULL;
static int acpi_enabled = 0;

/*
 * Standard ACPI 1.0 checksum: add all bytes, result must be 0.
 */
static int acpi_checksum(const uint8_t *data, uint32_t length) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++)
        sum += data[i];
    return sum;
}

/*
 * Search for the RSDP "RSD PTR " signature in a given memory region.
 */
static acpi_rsdp_t *acpi_find_rsdp_in_region(uintptr_t start, uintptr_t end) {
    for (uintptr_t addr = start; addr < end; addr += 16) {
        volatile const char *sig = (volatile const char *)addr;
        if (sig[0] == 'R' && sig[1] == 'S' && sig[2] == 'D' &&
            sig[3] == ' ' && sig[4] == 'P' && sig[5] == 'T' &&
            sig[6] == 'R' && sig[7] == ' ') {
            acpi_rsdp_t *rsdp = (acpi_rsdp_t *)addr;
            if (acpi_checksum((const uint8_t *)rsdp, 20) == 0)
                return rsdp;
        }
    }
    return NULL;
}

/*
 * Locate the RSDP by searching the EBDA and BIOS ROM.
 */
static acpi_rsdp_t *acpi_find_rsdp(void) {
    /* Search the upper BIOS area first (0x000E0000 - 0x000FFFFF) */
    acpi_rsdp_t *rsdp = acpi_find_rsdp_in_region(0x000E0000, 0x00100000);
    if (rsdp) return rsdp;

    /* Search the EBDA: the word at 0x040E contains the segment */
    uint16_t ebda_seg = *(volatile uint16_t *)0x40E;
    if (ebda_seg) {
        uintptr_t ebda_base = (uintptr_t)ebda_seg << 4;
        rsdp = acpi_find_rsdp_in_region(ebda_base, ebda_base + 1024);
        if (rsdp) return rsdp;
    }

    return NULL;
}

/*
 * Find an ACPI table by signature (e.g. "FACP", "APIC", "HPET")
 * by walking the RSDT or XSDT.
 */
static void *acpi_find_table(const char *signature) {
    acpi_rsdp_t *rsdp = acpi_find_rsdp();
    if (!rsdp) return NULL;

    uint32_t entry_count;
    uint32_t *entries32 = NULL;
    uint64_t *entries64 = NULL;
    int use_xsdt = 0;

    if (rsdp->revision >= 2) {
        /* ACPI 2.0+: prefer XSDT if the extended checksum passes */
        acpi_rsdp2_t *rsdp2 = (acpi_rsdp2_t *)rsdp;
        if (acpi_checksum((const uint8_t *)rsdp, rsdp2->length) == 0 && rsdp2->xsdt_address) {
            acpi_xsdt_t *xsdt = (acpi_xsdt_t *)(uintptr_t)rsdp2->xsdt_address;
            if (acpi_checksum((const uint8_t *)xsdt, xsdt->header.length) == 0) {
                entry_count = (xsdt->header.length - sizeof(acpi_sdt_t)) / 8;
                entries64 = xsdt->entries;
                use_xsdt = 1;
            }
        }
    }

    if (!use_xsdt) {
        /* Fall back to RSDT (32-bit) */
        acpi_rsdt_t *rsdt = (acpi_rsdt_t *)(uintptr_t)rsdp->rsdt_address;
        if (!rsdt) return NULL;
        entry_count = (rsdt->header.length - sizeof(acpi_sdt_t)) / 4;
        entries32 = rsdt->entries;
    }

    for (uint32_t i = 0; i < entry_count; i++) {
        acpi_sdt_t *table;
        if (use_xsdt)
            table = (acpi_sdt_t *)(uintptr_t)entries64[i];
        else
            table = (acpi_sdt_t *)(uintptr_t)entries32[i];

        if (!table) continue;

        /* Check signature */
        int match = 1;
        for (int j = 0; j < 4; j++) {
            if (table->signature[j] != signature[j]) {
                match = 0;
                break;
            }
        }
        if (match) return table;
    }

    return NULL;
}

/*
 * Parse the FADT and extract key PM information.
 */
static int acpi_parse_fadt(void) {
    acpi_fadt_t *f = (acpi_fadt_t *)acpi_find_table("FACP");
    if (!f) {
        f = (acpi_fadt_t *)acpi_find_table("FACP");
        if (!f) return -1;
    }
    fadt = f;

    kprint_timed_hex("ACPI: FADT at ", (uint64_t)f);
    kprint_timed_hex("ACPI: DSDT at ", (uint64_t)f->dsdt);
    kprint_timed_hex("ACPI: SMI_CMD port ", f->smi_cmd);
    kprint_timed_hex("ACPI: PM1a_CNT_BLK ", f->pm1a_cnt_blk);

    return 0;
}

/*
 * Enable ACPI by writing the acpi_enable value to the SMI_CMD port.
 * Some chipsets require this before ACPI shutdown/reboot works.
 */
static void acpi_enable_controller(void) {
    if (!fadt) return;
    if (fadt->smi_cmd == 0 || fadt->acpi_enable == 0) return;

    outb((uint16_t)fadt->smi_cmd, fadt->acpi_enable);
    for (uint32_t spin = 0; spin < 100000; spin++) {
        __asm__ volatile("pause");
    }
    kprint_timed("ACPI: controller enabled via SMI_CMD\n");
}

/*
 * Power off the system using ACPI PM1 control registers.
 * Writes SLP_TYP + SLP_EN to the PM1a_CNT register.
 * SLP_TYP = 5 (5 << 10) for S5 (soft-off), SLP_EN = 1 << 13.
 */
void acpi_poweroff(void) {
    if (!fadt || fadt->pm1a_cnt_blk == 0) {
        kprint_timed("ACPI poweroff: no PM1a_CNT block, trying triple-fault\n");
        __asm__ volatile("cli; hlt");
        return;
    }

    uint16_t pm1a_port = (uint16_t)fadt->pm1a_cnt_blk;
    /* SLP_TYP for S5 is typically 5, shifted left by 10, plus SLP_EN (1<<13) */
    uint16_t slp_typ = 5;
    uint16_t pm1a_val = (slp_typ << 10) | (1u << 13);

    outw(pm1a_port, pm1a_val);

    /* If PM1b exists, write there too */
    if (fadt->pm1b_cnt_blk) {
        outw((uint16_t)fadt->pm1b_cnt_blk, pm1a_val);
    }

    kprint_timed("ACPI: poweroff command sent, halting\n");
    for (;;) __asm__ volatile("cli; hlt");
}

/*
 * Reboot the system using ACPI RESET_REG (if available),
 * falling back to the PS/2 controller (0x64/0xFE) method.
 */
void acpi_reboot(void) {
    /* Try ACPI reset first if available */
    if (fadt) {
        /* Check if RESET_REG is valid (reg BDF at offset 116 in FADT) */
        acpi_gas_t *reset_reg = (acpi_gas_t *)((uint8_t *)fadt + 116);
        if (reset_reg->address) {
            uint8_t reset_val = fadt->reset_value;
            if (reset_reg->address_space_id == 1) {
                /* System IO space */
                outb((uint16_t)reset_reg->address, reset_val);
            } else if (reset_reg->address_space_id == 0) {
                /* System memory space */
                *(volatile uint8_t *)(uintptr_t)reset_reg->address = reset_val;
            }
            for (uint32_t spin = 0; spin < 100000; spin++) __asm__ volatile("pause");
        }
    }

    /* Fallback: PS/2 controller reset (triple-fault) */
    kprint_timed("ACPI: sending reset via keyboard controller\n");
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile("cli; hlt");
}

/*
 * Initialize the ACPI subsystem: find RSDP, parse FADT, enable controller.
 */
void acpi_init(void) {
    acpi_rsdp_t *rsdp = acpi_find_rsdp();
    if (!rsdp) {
        kprint_timed("ACPI: RSDP not found\n");
        return;
    }

    kprint_timed("ACPI: RSDP found, revision ");
    kprint_int(rsdp->revision);
    kprint("\n");

    if (acpi_parse_fadt() != 0) {
        kprint_timed("ACPI: FADT not found, PM unavailable\n");
        return;
    }

    acpi_enable_controller();
    acpi_enabled = 1;
    kprint_timed("ACPI: subsystem initialized\n");
}

int acpi_is_enabled(void) {
    return acpi_enabled;
}
