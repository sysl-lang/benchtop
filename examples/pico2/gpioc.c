// Every function of the SDK's hardware/gpio_coproc.h, each checked against the SIO register write or
// read the header documents it as equivalent to: the program keeps what OUT and OE must hold, reads
// them back through the SIO's registers after each instruction, and prints the name of any that
// differs. GPIO 0 and 1 (UART0) are left alone.

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"

#define LO 0x2bcd5678u
#define HI 0x00005a5cu

static uint64_t out, oe;
static int checks, bad;

static uint64_t sio_out(void) { return sio_hw->gpio_out | (uint64_t)sio_hw->gpio_hi_out << 32; }
static uint64_t sio_oe(void) { return sio_hw->gpio_oe | (uint64_t)sio_hw->gpio_hi_oe << 32; }

static void expect(const char *what) {
    checks++;
    if (sio_out() != out || sio_oe() != oe) {
        printf("MISMATCH after %s\n", what);
        bad++;
    }
}

static void lo(uint64_t *r, uint32_t x) { *r = (*r & ~0xffffffffull) | x; }
static void hi(uint64_t *r, uint32_t x) { *r = (*r & 0xffffffffull) | (uint64_t)x << 32; }

int main(void) {
    stdio_init_all();
    out = sio_out();
    oe = sio_oe();

    // Masks: OUT, then OE, low, high and both words.
    gpioc_lo_out_put(LO);  lo(&out, LO);       expect("gpioc_lo_out_put");
    gpioc_lo_out_xor(0xf0); out ^= 0xf0;       expect("gpioc_lo_out_xor");
    gpioc_lo_out_set(0x300); out |= 0x300;     expect("gpioc_lo_out_set");
    gpioc_lo_out_clr(0x8); out &= ~0x8ull;     expect("gpioc_lo_out_clr");
    gpioc_hi_out_put(HI);  hi(&out, HI);       expect("gpioc_hi_out_put");
    gpioc_hi_out_xor(0xf); out ^= 0xfull << 32; expect("gpioc_hi_out_xor");
    gpioc_hi_out_set(0x100); out |= 0x100ull << 32; expect("gpioc_hi_out_set");
    gpioc_hi_out_clr(0x4); out &= ~(0x4ull << 32); expect("gpioc_hi_out_clr");
    gpioc_hilo_out_put((uint64_t)HI << 32 | 0x1234567cu); out = (uint64_t)HI << 32 | 0x1234567cu; expect("gpioc_hilo_out_put");
    gpioc_hilo_out_xor(0x0000ff000000ff00ull); out ^= 0x0000ff000000ff00ull; expect("gpioc_hilo_out_xor");
    gpioc_hilo_out_set(0x0000800000000004ull); out |= 0x0000800000000004ull; expect("gpioc_hilo_out_set");
    gpioc_hilo_out_clr(0x0000000100000010ull); out &= ~0x0000000100000010ull; expect("gpioc_hilo_out_clr");

    gpioc_lo_oe_put(LO);   lo(&oe, LO);        expect("gpioc_lo_oe_put");
    gpioc_lo_oe_xor(0xf0); oe ^= 0xf0;         expect("gpioc_lo_oe_xor");
    gpioc_lo_oe_set(0x300); oe |= 0x300;       expect("gpioc_lo_oe_set");
    gpioc_lo_oe_clr(0x8);  oe &= ~0x8ull;      expect("gpioc_lo_oe_clr");
    gpioc_hi_oe_put(HI);   hi(&oe, HI);        expect("gpioc_hi_oe_put");
    gpioc_hi_oe_xor(0xf);  oe ^= 0xfull << 32; expect("gpioc_hi_oe_xor");
    gpioc_hi_oe_set(0x100); oe |= 0x100ull << 32; expect("gpioc_hi_oe_set");
    gpioc_hi_oe_clr(0x4);  oe &= ~(0x4ull << 32); expect("gpioc_hi_oe_clr");
    gpioc_hilo_oe_put((uint64_t)HI << 32 | 0x1234567cu); oe = (uint64_t)HI << 32 | 0x1234567cu; expect("gpioc_hilo_oe_put");
    gpioc_hilo_oe_xor(0x0000ff000000ff00ull); oe ^= 0x0000ff000000ff00ull; expect("gpioc_hilo_oe_xor");
    gpioc_hilo_oe_set(0x0000800000000004ull); oe |= 0x0000800000000004ull; expect("gpioc_hilo_oe_set");
    gpioc_hilo_oe_clr(0x0000000100000010ull); oe &= ~0x0000000100000010ull; expect("gpioc_hilo_oe_clr");

    // Single bits, a low pin and a high one; the conditional forms with 0 do nothing.
    for (uint pin = 5; pin <= 40; pin += 35) {
        uint64_t m = 1ull << pin;
        gpioc_bit_out_put(pin, 1);  out |= m;  expect("gpioc_bit_out_put 1");
        gpioc_bit_out_put(pin, 0);  out &= ~m; expect("gpioc_bit_out_put 0");
        gpioc_bit_out_xor(pin);     out ^= m;  expect("gpioc_bit_out_xor");
        gpioc_bit_out_set(pin);     out |= m;  expect("gpioc_bit_out_set");
        gpioc_bit_out_clr(pin);     out &= ~m; expect("gpioc_bit_out_clr");
        gpioc_bit_out_xor2(pin, 0);            expect("gpioc_bit_out_xor2 0");
        gpioc_bit_out_xor2(pin, 1); out ^= m;  expect("gpioc_bit_out_xor2 1");
        gpioc_bit_out_clr2(pin, 0);            expect("gpioc_bit_out_clr2 0");
        gpioc_bit_out_clr2(pin, 1); out &= ~m; expect("gpioc_bit_out_clr2 1");
        gpioc_bit_out_set2(pin, 0);            expect("gpioc_bit_out_set2 0");
        gpioc_bit_out_set2(pin, 1); out |= m;  expect("gpioc_bit_out_set2 1");

        gpioc_bit_oe_put(pin, 1);   oe |= m;   expect("gpioc_bit_oe_put 1");
        gpioc_bit_oe_put(pin, 0);   oe &= ~m;  expect("gpioc_bit_oe_put 0");
        gpioc_bit_oe_xor(pin);      oe ^= m;   expect("gpioc_bit_oe_xor");
        gpioc_bit_oe_set(pin);      oe |= m;   expect("gpioc_bit_oe_set");
        gpioc_bit_oe_clr(pin);      oe &= ~m;  expect("gpioc_bit_oe_clr");
        gpioc_bit_oe_xor2(pin, 0);             expect("gpioc_bit_oe_xor2 0");
        gpioc_bit_oe_xor2(pin, 1);  oe ^= m;   expect("gpioc_bit_oe_xor2 1");
        gpioc_bit_oe_clr2(pin, 0);             expect("gpioc_bit_oe_clr2 0");
        gpioc_bit_oe_clr2(pin, 1);  oe &= ~m;  expect("gpioc_bit_oe_clr2 1");
        gpioc_bit_oe_set2(pin, 0);             expect("gpioc_bit_oe_set2 0");
        gpioc_bit_oe_set2(pin, 1);  oe |= m;   expect("gpioc_bit_oe_set2 1");
    }

    // Indexed: the register named by a value, 0 low and 1 high.
    for (uint r = 0; r < 2; r++) {
        // The high word's GPIOs are 32-47, its bits 0-15.
        uint32_t v = r ? HI : LO;
        gpioc_index_out_put(r, v); (r ? hi : lo)(&out, v); expect("gpioc_index_out_put");
        gpioc_index_out_xor(r, 0x30); out ^= 0x30ull << (32 * r); expect("gpioc_index_out_xor");
        gpioc_index_out_set(r, 0x400); out |= 0x400ull << (32 * r); expect("gpioc_index_out_set");
        gpioc_index_out_clr(r, 0x8); out &= ~(0x8ull << (32 * r)); expect("gpioc_index_out_clr");
        gpioc_index_oe_put(r, v); (r ? hi : lo)(&oe, v); expect("gpioc_index_oe_put");
        gpioc_index_oe_xor(r, 0x30); oe ^= 0x30ull << (32 * r); expect("gpioc_index_oe_xor");
        gpioc_index_oe_set(r, 0x400); oe |= 0x400ull << (32 * r); expect("gpioc_index_oe_set");
        gpioc_index_oe_clr(r, 0x8); oe &= ~(0x8ull << (32 * r)); expect("gpioc_index_oe_clr");
    }

    // Reads: each the SIO register it names.
    checks++;
    if (gpioc_lo_out_get() != sio_hw->gpio_out || gpioc_hi_out_get() != sio_hw->gpio_hi_out || gpioc_hilo_out_get() != sio_out()) {
        printf("MISMATCH in the OUT reads\n");
        bad++;
    }
    checks++;
    if (gpioc_lo_oe_get() != sio_hw->gpio_oe || gpioc_hi_oe_get() != sio_hw->gpio_hi_oe || gpioc_hilo_oe_get() != sio_oe()) {
        printf("MISMATCH in the OE reads\n");
        bad++;
    }
    checks++;
    uint64_t in = sio_hw->gpio_in | (uint64_t)sio_hw->gpio_hi_in << 32;
    if (gpioc_lo_in_get() != sio_hw->gpio_in || gpioc_hi_in_get() != sio_hw->gpio_hi_in || gpioc_hilo_in_get() != in) {
        printf("MISMATCH in the IN reads\n");
        bad++;
    }

    printf("gpioc: %d checks, %d mismatches; out %016llx oe %016llx\n", checks, bad,
           (unsigned long long)sio_out(), (unsigned long long)sio_oe());
    uart_tx_wait_blocking(uart0);
    __breakpoint();
}
