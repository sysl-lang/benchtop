// A busy loop for measuring how fast code runs from the XIP cache used as SRAM: code, data and stack
// in the 16 KB at XIP_SRAM_BASE (0x13ffc000), as `xip_sram` is linked, with no waiting. Each round
// sorts a small table, sums it through calls and returns, and stores the running checksum, so loads,
// stores, direct and indirect branches all run from XIP_SRAM. Built a second time as an SRAM image
// (`sram_loop`), the same instructions measure the SRAM path beside it.
#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"

#define N 64
#ifndef ROUNDS
#define ROUNDS 20000
#endif

static uint32_t table[N];
static volatile uint32_t checksum;

static uint32_t __attribute__((noinline)) mix(uint32_t x, uint32_t k) {
    return (x ^ (x >> 7)) * 2654435761u + k;
}

static void __attribute__((noinline)) fill(uint32_t seed) {
    for (int i = 0; i < N; i++) table[i] = seed = mix(seed, (uint32_t)i);
}

static void __attribute__((noinline)) sort(void) {
    for (int i = 1; i < N; i++) {
        uint32_t v = table[i];
        int j = i - 1;
        while (j >= 0 && table[j] > v) {
            table[j + 1] = table[j];
            j--;
        }
        table[j + 1] = v;
    }
}

static uint32_t __attribute__((noinline)) sum(void) {
    uint32_t s = 0;
    for (int i = 0; i < N; i++) s = mix(s, table[i]);
    return s;
}

int main(void) {
    stdio_init_all();
    printf("loop begins: main at %08x\n", (unsigned)(uintptr_t)main);
    for (uint32_t round = 0; round < ROUNDS; round++) {
        fill(round);
        sort();
        checksum += sum();
    }
    printf("checksum %08x\nPASSED\n", (unsigned)checksum);
    return 0;
}
