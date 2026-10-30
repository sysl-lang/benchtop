// benchtop's own: looks functions up in the RP2040's boot ROM by their two-letter codes, calls each,
// and prints what it returned, for chip/pico_tests.sysl to check against the host's arithmetic. The
// float and double functions come from the ROM's soft float and double tables (`rom_data_lookup`);
// the bit operations and memset/memcpy from its function table (`rom_func_lookup`).

#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/bootrom/sf_table.h"

typedef uint32_t (*bits_fn)(uint32_t);
typedef float (*float2_fn)(float, float);
typedef float (*float1_fn)(float);
typedef double (*double2_fn)(double, double);
typedef double (*double1_fn)(double);
typedef uint8_t *(*memset_fn)(uint8_t *, uint8_t, uint32_t);
typedef uint8_t *(*memcpy_fn)(uint8_t *, const uint8_t *, uint32_t);

typedef union { float f; uint32_t u; } float_bits;
typedef union { double d; uint64_t u; } double_bits;

// Inputs the compiler cannot fold.
static volatile uint32_t words[] = {0xF0F01234, 0x00012340, 0x12345678, 0, 0xFFFFFFFF};
static volatile float floats[] = {1.0f, 3.0f, 2.0f, 0.1f, 0.2f};
static volatile double doubles[] = {1.0, 3.0, 2.0, 0.1};

static uint32_t f(float x) { float_bits b = {.f = x}; return b.u; }

static void print_double(const char *what, double x) {
    double_bits b = {.d = x};
    printf("%s = %08lx%08lx\n", what, (unsigned long)(b.u >> 32), (unsigned long)(b.u & 0xFFFFFFFF));
}

int main(void) {
    stdio_init_all();
    printf("rom version %d\n", rp2040_rom_version());

    bits_fn popcount = (bits_fn)rom_func_lookup(ROM_FUNC_POPCOUNT32);
    bits_fn clz = (bits_fn)rom_func_lookup(ROM_FUNC_CLZ32);
    bits_fn ctz = (bits_fn)rom_func_lookup(ROM_FUNC_CTZ32);
    bits_fn reverse = (bits_fn)rom_func_lookup(ROM_FUNC_REVERSE32);
    printf("popcount32 at %08lx\n", (unsigned long)(uintptr_t)popcount);
    for (int i = 0; i < 5; i++) {
        uint32_t x = words[i];
        printf("%08lx: popcount %lu clz %lu ctz %lu reverse %08lx\n", (unsigned long)x, (unsigned long)popcount(x),
               (unsigned long)clz(x), (unsigned long)ctz(x), (unsigned long)reverse(x));
    }

    uint8_t *sf = (uint8_t *)rom_data_lookup(ROM_TABLE_CODE('S', 'F'));
    float2_fn fadd = *(float2_fn *)(sf + SF_TABLE_FADD);
    float2_fn fmul = *(float2_fn *)(sf + SF_TABLE_FMUL);
    float2_fn fdiv = *(float2_fn *)(sf + SF_TABLE_FDIV);
    float1_fn fsqrt = *(float1_fn *)(sf + SF_TABLE_FSQRT);
    printf("fdiv(1, 3) = %08lx\n", (unsigned long)f(fdiv(floats[0], floats[1])));
    printf("fsqrt(2) = %08lx\n", (unsigned long)f(fsqrt(floats[2])));
    printf("fadd(0.1, 0.2) = %08lx\n", (unsigned long)f(fadd(floats[3], floats[4])));
    printf("fmul(0.1, 3) = %08lx\n", (unsigned long)f(fmul(floats[3], floats[1])));

    uint8_t *sd = (uint8_t *)rom_data_lookup(ROM_TABLE_CODE('S', 'D'));
    double2_fn ddiv = *(double2_fn *)(sd + SF_TABLE_FDIV);
    double2_fn dmul = *(double2_fn *)(sd + SF_TABLE_FMUL);
    double1_fn dsqrt = *(double1_fn *)(sd + SF_TABLE_FSQRT);
    print_double("ddiv(1, 3)", ddiv(doubles[0], doubles[1]));
    print_double("dsqrt(2)", dsqrt(doubles[2]));
    print_double("dmul(0.1, 3)", dmul(doubles[3], doubles[1]));

    static uint8_t from[16], to[16];
    memset_fn rom_memset = (memset_fn)rom_func_lookup(ROM_FUNC_MEMSET);
    memcpy_fn rom_memcpy = (memcpy_fn)rom_func_lookup(ROM_FUNC_MEMCPY);
    for (int i = 0; i < 16; i++) from[i] = (uint8_t)(i * 17);
    rom_memset(to, 0xA5, 16);
    rom_memcpy(to + 3, from + 1, 9);
    printf("mem");
    for (int i = 0; i < 16; i++) printf(" %02x", to[i]);
    printf("\n");

    // The SDK's own float and double through its default implementation, which calls the ROM's.
    volatile float third = floats[0] / floats[1];
    volatile double root = doubles[2] / doubles[1];
    printf("sdk %08lx\n", (unsigned long)f(third));
    print_double("sdk", root);
    printf("done\n");
    for (;;) sleep_ms(1000);
}
