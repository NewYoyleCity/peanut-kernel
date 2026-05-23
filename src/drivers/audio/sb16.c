/*
 * Creative Sound Blaster 16 (SB16) DSP driver
 *
 * SB16 is an ISA DMA-based sound card widely supported in QEMU and real
 * hardware.  This driver implements:
 *   - DSP version detection via command 0xE1
 *   - 8-bit and 16-bit PCM playback at configurable sample rates
 *   - IRQ-driven playback complete notification (stub)
 *
 * Registers:
 *   0x220  – DSP reset (write) / read data (read)
 *   0x226  – DSP read status (bit 7 set = data available)
 *   0x22C  – DSP write buffer (bit 7 set = ready for write)
 *   0x22E  – DSP data available (bit 7 set = data ready)
 *   0x22A  – Interrupt acknowledge (read 8-bit)
 *
 * DMA is used for transfer; we program the 8237 DMA controller channels
 * 1 (8-bit) or 5 (16-bit, on secondary controller).
 */

#include "sb16.h"
#include "drivers/bus/io.h"
#include "freelib/kstdio.h"
#include "freelib/kalloc.h"
#include "freelib/kstdint.h"

/* Base I/O port for SB16 (configurable, typically 0x220) */
#define SB16_BASE      0x220

/* DSP registers relative to base */
#define DSP_RESET      0x06
#define DSP_READ       0x0A
#define DSP_WRITE      0x0C
#define DSP_READ_STATUS 0x0E
#define DSP_DATA_AVAIL 0x0E
#define DSP_ACK16      0x0F
#define DSP_MIXER      0x04
#define DSP_MIXER_DATA 0x05

/* DMA controller ports (primary) */
#define DMA1_MASK      0x0A
#define DMA1_MODE      0x0B
#define DMA1_CLRFF     0x0C
#define DMA1_ADDR      0x02
#define DMA1_COUNT     0x03
#define DMA1_PAGE      0x83

/* DMA controller ports (secondary, for 16-bit) */
#define DMA2_MASK      0xD4
#define DMA2_MODE      0xD6
#define DMA2_CLRFF     0xD8
#define DMA2_ADDR      0xC4
#define DMA2_COUNT     0xC6
#define DMA2_PAGE      0x8B

#define SB16_CMD_GET_VERSION   0xE1
#define SB16_CMD_SPEAKER_ON    0xD1
#define SB16_CMD_SPEAKER_OFF   0xD3
#define SB16_CMD_8BIT_PLAY     0x0C   /* 8-bit PCM, auto-init */
#define SB16_CMD_16BIT_PLAY    0xB0   /* 16-bit PCM, auto-init, FIFO */
#define SB16_CMD_STOP_8BIT     0xD0
#define SB16_CMD_STOP_16BIT    0xD5
#define SB16_CMD_SET_RATE      0x41

/* Sample buffer size */
#define SB16_BUF_SIZE  65536

static int sb16_found = 0;
static uint8_t *sb16_buffer = NULL;

/*
 * Wait until the DSP is ready to receive a write command.
 */
static void dsp_write_wait(void) {
    for (uint32_t spin = 0; spin < 100000; spin++) {
        if ((inb(SB16_BASE + DSP_WRITE) & 0x80) == 0)
            return;
    }
}

/*
 * Write a byte to the DSP.
 */
static void dsp_write(uint8_t val) {
    dsp_write_wait();
    outb(SB16_BASE + DSP_WRITE, val);
}

/*
 * Wait until data is available from the DSP.
 */
static void dsp_read_wait(void) {
    for (uint32_t spin = 0; spin < 100000; spin++) {
        if (inb(SB16_BASE + DSP_READ_STATUS) & 0x80)
            return;
    }
}

/*
 * Read a byte from the DSP.
 */
static uint8_t dsp_read(void) {
    dsp_read_wait();
    return inb(SB16_BASE + DSP_READ);
}

/*
 * Reset the DSP by toggling the reset bit.
 */
static int dsp_reset(void) {
    outb(SB16_BASE + DSP_RESET, 1);
    for (uint32_t spin = 0; spin < 10000; spin++) __asm__ volatile("pause");
    outb(SB16_BASE + DSP_RESET, 0);
    if (dsp_read() != 0xAA) {
        kprint("SB16: DSP reset failed\n");
        return -1;
    }
    return 0;
}

/*
 * Program the 8237 DMA controller for an 8-bit transfer (channel 1).
 */
static void dma_program_8bit(uint32_t phys_addr, uint32_t length) {
    uint32_t page = phys_addr >> 16;
    uint32_t offset = phys_addr & 0xFFFF;
    uint32_t count = length - 1;

    outb(DMA1_MASK,  0x05);        /* Mask channel 1 */
    outb(DMA1_CLRFF, 0xFF);        /* Clear flip-flop */
    outb(DMA1_ADDR,  offset & 0xFF);       /* Low byte of address */
    outb(DMA1_ADDR,  (offset >> 8) & 0xFF); /* High byte of address */
    outb(DMA1_PAGE,  page & 0xFF);          /* Page register */
    outb(DMA1_COUNT, count & 0xFF);         /* Low byte of count */
    outb(DMA1_COUNT, (count >> 8) & 0xFF);  /* High byte of count */
    outb(DMA1_MODE,  0x48 | 0x02);           /* Single, read, auto-init, channel 1 */
    outb(DMA1_MASK,  0x01);                  /* Unmask channel 1 */
}

/*
 * Program the 8237 DMA controller for a 16-bit transfer (channel 5).
 */
static void dma_program_16bit(uint32_t phys_addr, uint32_t length) {
    uint32_t page = phys_addr >> 16;
    uint32_t offset = phys_addr & 0xFFFF;
    uint32_t count = length - 1;

    outb(DMA2_MASK,  0x05);        /* Mask channel 5 */
    outb(DMA2_CLRFF, 0xFF);
    outb(DMA2_ADDR,  (offset & 0xFF) >> 1);           /* Word address (16-bit) */
    outb(DMA2_ADDR,  ((offset >> 8) & 0xFF) >> 1);
    outb(DMA2_PAGE,  page & 0xFF);
    outb(DMA2_COUNT, count & 0xFF);
    outb(DMA2_COUNT, (count >> 8) & 0xFF);
    outb(DMA2_MODE,  0x48 | 0x02);                     /* Single, read, auto-init, ch 5 */
    outb(DMA2_MASK,  0x01);
}

/*
 * Query the DSP version.
 */
static uint16_t dsp_version(void) {
    dsp_write(SB16_CMD_GET_VERSION);
    uint8_t major = dsp_read();
    uint8_t minor = dsp_read();
    return (uint16_t)major << 8 | minor;
}

/*
 * Start 8-bit unsigned PCM playback at a given sample rate.
 * 'buffer' must be DMA-able (below 16 MB for 8-bit).
 */
int sb16_play_8bit(uint8_t *buffer, uint32_t length, uint32_t sample_rate) {
    if (!sb16_found || !buffer || length == 0) return -1;

    /* Set sample rate */
    dsp_write(SB16_CMD_SET_RATE);
    dsp_write((uint8_t)(sample_rate >> 8));
    dsp_write((uint8_t)(sample_rate & 0xFF));

    /* Program DMA channel 1 */
    uint32_t phys = (uint32_t)(uint64_t)buffer;
    dma_program_8bit(phys, length);

    /* Start 8-bit auto-init playback */
    dsp_write(SB16_CMD_8BIT_PLAY);
    dsp_write((uint8_t)(length & 0xFF));
    dsp_write((uint8_t)((length >> 8) & 0xFF));

    return 0;
}

/*
 * Start 16-bit signed PCM playback.
 */
int sb16_play_16bit(uint8_t *buffer, uint32_t length, uint32_t sample_rate) {
    if (!sb16_found || !buffer || length == 0) return -1;

    /* Set sample rate */
    dsp_write(SB16_CMD_SET_RATE);
    dsp_write((uint8_t)(sample_rate >> 8));
    dsp_write((uint8_t)(sample_rate & 0xFF));

    uint32_t phys = (uint32_t)(uint64_t)buffer;
    dma_program_16bit(phys, length);

    /* 16-bit auto-init playback: command = 0xB0, mode = 0x00 (mono) */
    uint8_t mode = 0x00;
    dsp_write(SB16_CMD_16BIT_PLAY);
    dsp_write(mode);
    dsp_write((uint8_t)(length & 0xFF));
    dsp_write((uint8_t)((length >> 8) & 0xFF));

    return 0;
}

/*
 * Stop playback.
 */
void sb16_stop(void) {
    if (!sb16_found) return;
    dsp_write(SB16_CMD_STOP_8BIT);
    dsp_write(SB16_CMD_STOP_16BIT);
}

/*
 * Initialize SB16: reset DSP, check version, enable speaker.
 */
void sb16_init(void) {
    kprint("SB16: probing at I/O 0x220...\n");

    if (dsp_reset() != 0) {
        kprint("SB16: not found\n");
        return;
    }

    uint16_t ver = dsp_version();
    uint8_t major = (uint8_t)(ver >> 8);
    uint8_t minor = (uint8_t)(ver & 0xFF);

    if (major < 4) {
        kprint("SB16: DSP version ");
        kprint_int(major);
        kprint(".");
        kprint_int(minor);
        kprint(" too old (need 4.x)\n");
        return;
    }

    sb16_buffer = (uint8_t *)kalloc(SB16_BUF_SIZE);
    if (!sb16_buffer) {
        kprint("SB16: buffer allocation failed\n");
        return;
    }

    dsp_write(SB16_CMD_SPEAKER_ON);
    sb16_found = 1;

    kprint("SB16: DSP v");
    kprint_int(major);
    kprint(".");
    kprint_int(minor);
    kprint(" initialized at I/O 0x220\n");
}

int sb16_is_present(void) {
    return sb16_found;
}
