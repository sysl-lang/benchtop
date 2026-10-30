// benchtop's own: the RP2040's flash through its SSI, using no boot ROM function, since benchtop's
// stand-in ROM carries none (README.md). Commands go out in direct mode the way the SDK's
// hardware_flash sends them (`flash_do_cmd`: chip select forced through IO_QSPI, at most 14 bytes in
// flight); XIP is entered by the SDK's own second stage, `boot2_w25q080.S`, linked in and called as the
// SDK's `flash_enable_xip_via_boot2` calls its copy. Every line it prints is a check.

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/resets.h"
#include "hardware/vreg.h"
#include "hardware/structs/ssi.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/structs/vreg_and_chip_reset.h"
#include "hardware/regs/addressmap.h"

// pico-sdk's src/rp2040/boot_stage2/boot2_w25q080.S: quad I/O continuous read (EBh, mode A0h).
void _stage2_boot(void);

// The sector the program erases and programs: 1 MB into the flash. Its first page holds data, its
// second a function.
#define OFFSET 0x100000u
#define CODE (OFFSET + 256u)

// The QSPI pins on the SSI, as the ROM's `connect_internal_flash` leaves them: FUNCSEL 0 (XIP) on
// SCLK, SS and SD0-3, no overrides.
static void connect_flash(void) {
    reset_unreset_block_num_wait_blocking(RESET_IO_QSPI);
    reset_unreset_block_num_wait_blocking(RESET_PADS_QSPI);
    for (int i = 0; i < 6; i++) ioqspi_hw->io[i].ctrl = 0;
}

static void cs_force(bool high) {
    hw_write_masked(&ioqspi_hw->io[1].ctrl,
        (high ? IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_VALUE_HIGH : IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_VALUE_LOW) << IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_LSB,
        IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_BITS);
}

// One command frame: chip select low, `count` bytes each way, chip select high (`flash_do_cmd`).
static void do_cmd(const uint8_t *txbuf, uint8_t *rxbuf, size_t count) {
    cs_force(false);
    size_t tx_remaining = count;
    size_t rx_remaining = count;
    const size_t max_in_flight = 16 - 2;
    while (tx_remaining || rx_remaining) {
        uint32_t flags = ssi_hw->sr;
        bool can_put = flags & SSI_SR_TFNF_BITS;
        bool can_get = flags & SSI_SR_RFNE_BITS;
        if (can_put && tx_remaining && rx_remaining - tx_remaining < max_in_flight) {
            ssi_hw->dr0 = *txbuf++;
            --tx_remaining;
        }
        if (can_get && rx_remaining) {
            uint8_t b = (uint8_t)ssi_hw->dr0;
            if (rxbuf) *rxbuf++ = b;
            --rx_remaining;
        }
    }
    cs_force(true);
}

// The SSI in direct mode (Motorola SPI, 8-bit frames, transmit and receive, slave 0), and the flash out
// of continuous read mode: FFh where its mode bits go.
static void exit_xip(void) {
    ssi_hw->ssienr = 0;
    ssi_hw->baudr = 4;
    ssi_hw->ctrlr0 = (7u << SSI_CTRLR0_DFS_32_LSB) | (SSI_CTRLR0_TMOD_VALUE_TX_AND_RX << SSI_CTRLR0_TMOD_LSB);
    ssi_hw->ser = 1;
    ssi_hw->ssienr = 1;
    static const uint8_t ff[4] = {0xff, 0xff, 0xff, 0xff};
    do_cmd(ff, NULL, 4);
}

// The cache flushed, and chip select back to the SSI, as the ROM's `flash_flush_cache` does.
static void flush_cache(void) {
    xip_ctrl_hw->flush = 1;
    (void)xip_ctrl_hw->flush;
    hw_clear_bits(&ioqspi_hw->io[1].ctrl, IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_BITS);
}

static uint8_t read_status(uint8_t cmd) {
    uint8_t tx[2] = {cmd, 0};
    uint8_t rx[2];
    do_cmd(tx, rx, 2);
    return rx[1];
}

static void write_enable(void) {
    static const uint8_t we = 0x06;
    do_cmd(&we, NULL, 1);
}

static void wait_ready(void) {
    while (read_status(0x05) & 1)
        ;
}

static void erase_sector(uint32_t at) {
    write_enable();
    uint8_t tx[4] = {0x20, at >> 16, at >> 8, at};
    do_cmd(tx, NULL, 4);
    wait_ready();
}

static void program_page(uint32_t at, const uint8_t *data, size_t n) {
    static uint8_t tx[4 + 256];
    write_enable();
    tx[0] = 0x02;
    tx[1] = at >> 16;
    tx[2] = at >> 8;
    tx[3] = at;
    memcpy(tx + 4, data, n);
    do_cmd(tx, NULL, 4 + n);
    wait_ready();
}

static void read_direct(uint32_t at, uint8_t *out, size_t n) {
    static uint8_t tx[4 + 16];
    static uint8_t rx[4 + 16];
    memset(tx, 0, sizeof tx);
    tx[0] = 0x03;
    tx[1] = at >> 16;
    tx[2] = at >> 8;
    tx[3] = at;
    do_cmd(tx, rx, 4 + n);
    memcpy(out, rx + 4, n);
}

static void print_bytes(const char *label, const uint8_t *b, size_t n) {
    printf("%s", label);
    for (size_t i = 0; i < n; i++) printf(" %02x", b[i]);
    printf("\n");
}

// A Thumb function that returns `k`: movs r0, #k; bx lr.
static void returns(uint8_t *page, uint8_t k) {
    memset(page, 0xff, 256);
    page[0] = k;
    page[1] = 0x20;
    page[2] = 0x70;
    page[3] = 0x47;
}

static int call(uint32_t address) {
    int (*f)(void) = (int (*)(void))(address | 1);
    return f();
}

int main(void) {
    stdio_init_all();
    printf("ssi_flash\n");

    connect_flash();
    exit_xip();
    uint8_t tx[13] = {0x9f};
    uint8_t rx[13];
    do_cmd(tx, rx, 4);
    print_bytes("JEDEC ID", rx + 1, 3);
    memset(tx, 0, sizeof tx);
    tx[0] = 0x4b;
    do_cmd(tx, rx, 13);
    print_bytes("unique ID", rx + 5, 8);
    printf("status %02x %02x\n", read_status(0x05), read_status(0x35));

    static uint8_t page[256];
    erase_sector(OFFSET);
    for (int i = 0; i < 256; i++) page[i] = i ^ 0x5a;
    program_page(OFFSET, page, 256);
    returns(page, 42);
    program_page(CODE, page, 256);
    uint8_t got[8];
    read_direct(OFFSET, got, 8);
    print_bytes("direct read", got, 8);

    flush_cache();
    _stage2_boot();
    static const uint32_t aliases[4] = {XIP_BASE, XIP_NOALLOC_BASE, XIP_NOCACHE_BASE, XIP_NOCACHE_NOALLOC_BASE};
    for (int k = 0; k < 4; k++) printf("XIP %08lx: %08lx\n", (unsigned long)(aliases[k] + OFFSET), (unsigned long)*(volatile uint32_t *)(aliases[k] + OFFSET));
    printf("call from flash: %d\n", call(XIP_BASE + CODE));

    exit_xip();
    printf("status 2 after boot2: %02x\n", read_status(0x35));
    erase_sector(OFFSET);
    returns(page, 7);
    program_page(CODE, page, 256);
    flush_cache();
    _stage2_boot();
    printf("after erase: %08lx\n", (unsigned long)*(volatile uint32_t *)(XIP_BASE + OFFSET));
    printf("call from flash: %d\n", call(XIP_BASE + CODE));

    xip_ctrl_hw->stream_addr = XIP_BASE + CODE;
    xip_ctrl_hw->stream_ctr = 2;
    uint32_t first = xip_ctrl_hw->stream_fifo;
    uint32_t second = xip_ctrl_hw->stream_fifo;
    printf("stream %08lx %08lx, stat %02lx\n", (unsigned long)first, (unsigned long)second, (unsigned long)xip_ctrl_hw->stat);

    hw_clear_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_EN_BITS);
    volatile uint16_t *sram = (volatile uint16_t *)XIP_SRAM_BASE;
    sram[0] = 0x2063;
    sram[1] = 0x4770;
    *(volatile uint32_t *)(XIP_SRAM_BASE + 0x3ffc) = 0xc0ffee;
    printf("call from XIP SRAM: %d, last word %08lx\n", call(XIP_SRAM_BASE), (unsigned long)*(volatile uint32_t *)(XIP_SRAM_BASE + 0x3ffc));
    hw_set_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_EN_BITS);
    xip_ctrl_hw->flush = 1;
    printf("XIP ctrl %02lx stat %02lx\n", (unsigned long)xip_ctrl_hw->ctrl, (unsigned long)xip_ctrl_hw->stat);

    printf("vreg %08lx bod %08lx chip_reset %08lx\n", (unsigned long)vreg_and_chip_reset_hw->vreg, (unsigned long)vreg_and_chip_reset_hw->bod, (unsigned long)vreg_and_chip_reset_hw->chip_reset);
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    printf("vreg %08lx\n", (unsigned long)vreg_and_chip_reset_hw->vreg);
    printf("done\n");
    return 0;
}
