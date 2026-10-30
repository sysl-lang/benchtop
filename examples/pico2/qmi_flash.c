// A flash program that reprograms its own flash through the QMI's direct mode, without the boot ROM:
// it reads the flash's JEDEC and unique IDs, erases a 4 KB sector 1 MB in, programs a page there,
// leaves direct mode, invalidates the XIP cache over the sector, and reads the page back through
// every XIP window: the cached one, the uncached one, the untranslated one, the streaming FIFO
// (through XIP_CTRL and through XIP_AUX) and a direct-mode 03h read. The code that drives direct mode
// runs from SRAM (`__not_in_flash_func`), as it must on silicon, where memory-mapped flash accesses
// are bus errors while direct mode is on.
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/structs/xip_aux.h"
#include "hardware/sync.h"
#include "hardware/xip_cache.h"

#define TEST_OFFSET (1024u * 1024u)
#define PAGE 256u

static uint8_t jedec[3];
static uint8_t uid[8];
static uint8_t direct_read[8];

static void __not_in_flash_func(chip_select)(bool asserted) {
    if (asserted)
        hw_set_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS0N_BITS);
    else
        hw_clear_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS0N_BITS);
}

// One serial frame on chip select 0: `count` bytes out of `tx`, and the same number into `rx` (or
// dropped where `rx` is NULL).
static void __not_in_flash_func(frame)(const uint8_t *tx, uint8_t *rx, size_t count) {
    chip_select(true);
    size_t tx_left = count, rx_left = count;
    while (tx_left || rx_left) {
        uint32_t flags = qmi_hw->direct_csr;
        if (tx_left && !(flags & QMI_DIRECT_CSR_TXFULL_BITS)) {
            qmi_hw->direct_tx = *tx++;
            --tx_left;
        }
        if (rx_left && !(flags & QMI_DIRECT_CSR_RXEMPTY_BITS)) {
            uint8_t b = (uint8_t)qmi_hw->direct_rx;
            if (rx)
                *rx++ = b;
            --rx_left;
        }
    }
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {
    }
    chip_select(false);
}

static void __not_in_flash_func(command)(uint8_t cmd) {
    frame(&cmd, NULL, 1);
}

static void __not_in_flash_func(wait_ready)(void) {
    uint8_t tx[2], rx[2];
    tx[0] = 0x05;
    tx[1] = 0;
    do {
        frame(tx, rx, 2);
    } while (rx[1] & 1);
}

static void __not_in_flash_func(address_command)(uint8_t cmd, uint32_t at, uint8_t *buf) {
    buf[0] = cmd;
    buf[1] = (uint8_t)(at >> 16);
    buf[2] = (uint8_t)(at >> 8);
    buf[3] = (uint8_t)at;
}

// memset and memcpy are in flash, which cannot be read while direct mode is on: these copy byte by
// byte through volatile pointers, so the compiler does not turn them back into the library calls.
static void __not_in_flash_func(fill)(uint8_t *to, uint8_t value, size_t n) {
    volatile uint8_t *p = to;
    for (size_t i = 0; i < n; i++)
        p[i] = value;
}

static void __not_in_flash_func(copy)(uint8_t *to, const uint8_t *from, size_t n) {
    volatile uint8_t *p = to;
    for (size_t i = 0; i < n; i++)
        p[i] = from[i];
}

static void __no_inline_not_in_flash_func(reprogram)(const uint8_t *page) {
    static uint8_t tx[4 + PAGE];
    static uint8_t rx[13];
    uint32_t irq = save_and_disable_interrupts();
    hw_set_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_EN_BITS);

    // Leave continuous read mode, should the flash be in it.
    fill(tx, 0xff, 4);
    frame(tx, NULL, 4);

    fill(tx, 0, 13);
    tx[0] = 0x9f;
    frame(tx, rx, 4);
    copy(jedec, rx + 1, 3);

    tx[0] = 0x4b;
    frame(tx, rx, 13);
    copy(uid, rx + 5, 8);

    command(0x06);
    address_command(0x20, TEST_OFFSET, tx);
    frame(tx, NULL, 4);
    wait_ready();

    command(0x06);
    address_command(0x02, TEST_OFFSET, tx);
    copy(tx + 4, page, PAGE);
    frame(tx, NULL, 4 + PAGE);
    wait_ready();

    address_command(0x03, TEST_OFFSET, tx);
    fill(tx + 4, 0, 8);
    static uint8_t got[12];
    frame(tx, got, 12);
    copy(direct_read, got + 4, 8);

    hw_clear_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_EN_BITS);
    xip_cache_invalidate_range(TEST_OFFSET, 4096);
    restore_interrupts(irq);
}

static bool same(const volatile uint8_t *at, const uint8_t *want, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (at[i] != want[i])
            return false;
    return true;
}

int main(void) {
    stdio_init_all();
    printf("qmi_flash begins: xip_ctrl %08x\n", (unsigned)xip_ctrl_hw->ctrl);

    static uint8_t page[PAGE];
    for (unsigned i = 0; i < PAGE; i++)
        page[i] = (uint8_t)(i * 7 + 3);
    reprogram(page);

    printf("jedec %02x %02x %02x\n", jedec[0], jedec[1], jedec[2]);
    printf("uid %02x%02x%02x%02x%02x%02x%02x%02x\n", uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6], uid[7]);
    printf("direct read %s\n", same(direct_read, page, 8) ? "ok" : "BAD");
    printf("cached %s\n", same((const volatile uint8_t *)(XIP_BASE + TEST_OFFSET), page, PAGE) ? "ok" : "BAD");
    printf("uncached %s\n", same((const volatile uint8_t *)(XIP_NOCACHE_NOALLOC_BASE + TEST_OFFSET), page, PAGE) ? "ok" : "BAD");
    printf("untranslated %s\n", same((const volatile uint8_t *)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + TEST_OFFSET), page, PAGE) ? "ok" : "BAD");
    const volatile uint8_t *rest = (const volatile uint8_t *)(XIP_BASE + TEST_OFFSET + PAGE);
    bool erased = true;
    for (unsigned i = 0; i < 4096 - PAGE; i++)
        if (rest[i] != 0xff)
            erased = false;
    printf("erased %s\n", erased ? "ok" : "BAD");

    // The streaming FIFO, two words through XIP_CTRL and two through XIP_AUX.
    xip_ctrl_hw->stream_addr = XIP_BASE + TEST_OFFSET;
    xip_ctrl_hw->stream_ctr = 4;
    uint32_t words[4];
    for (int i = 0; i < 4; i++) {
        while (xip_ctrl_hw->stat & XIP_STAT_FIFO_EMPTY) {
        }
        words[i] = i < 2 ? xip_ctrl_hw->stream_fifo : xip_aux_hw->stream;
    }
    printf("stream %s, ctr %u\n", same((const uint8_t *)words, page, 16) ? "ok" : "BAD", (unsigned)xip_ctrl_hw->stream_ctr);

    // Two cache lines pinned at XIP_SRAM_BASE hold what is written to them.
    xip_cache_pin_range(XIP_SRAM_BASE - XIP_BASE, 16);
    volatile uint32_t *sram = (volatile uint32_t *)XIP_SRAM_BASE;
    sram[0] = 0x12345678;
    sram[3] = 0x9abcdef0;
    printf("pinned %s\n", sram[0] == 0x12345678 && sram[3] == 0x9abcdef0 ? "ok" : "BAD");

    printf("PASSED\n");
    return 0;
}
