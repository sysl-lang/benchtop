// benchtop's own: the RP2040 boot ROM cross-check. Every entry of the chip's own ROM's soft float and
// double tables, and its bit and memory functions, is called on a fixed, seeded set of inputs beside
// benchtop's own ROM routines (sysl, compiled into this firmware from `ours/`), and the two answers are
// compared bit for bit.
//
// On a real Pico the chip's ROM is Raspberry Pi's, so this measures where benchtop's ROM agrees with it;
// on benchtop's `pico` board both sides are benchtop's. Either way the last line's CRC is of benchtop's
// answers alone, so the two runs must print the same one.
//
// Output, on UART0 and (on silicon) USB CDC: one line per function, `<name> <n> pass <k> mismatch`, up
// to five mismatches each with the inputs and both answers, then
// `CROSSCHECK <total> <mismatches> <crc32 of ours>`.

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/stdio_uart.h"
#include "pico/stdio_usb.h"

extern const uint32_t ours_sf_table[32];
extern const uint32_t ours_sd_table[32];
uint32_t ours_popcount32(uint32_t);
uint32_t ours_reverse32(uint32_t);
uint32_t ours_clz32(uint32_t);
uint32_t ours_ctz32(uint32_t);
uint8_t *ours_memset(uint8_t *, uint8_t, uint32_t);
uint8_t *ours_memset4(uint8_t *, uint8_t, uint32_t);
uint8_t *ours_memcpy(uint8_t *, const uint8_t *, uint32_t);
uint8_t *ours_memcpy44(uint8_t *, const uint8_t *, uint32_t);

// Random inputs per function, after the fixed edge cases.
#define RANDOM 1000

static const char *const SF_NAMES[32] = {
    "fadd", "fsub", "fmul", "fdiv", "fcmp_fast", "fcmp_fast_flags", "fsqrt", "float2int", "float2fix",
    "float2uint", "float2ufix", "int2float", "fix2float", "uint2float", "ufix2float", "fcos", "fsin",
    "ftan", "fsincos", "fexp", "fln", "fcmp_basic", "fatan2", "int642float", "fix642float",
    "uint642float", "ufix642float", "float2int64", "float2fix64", "float2uint64", "float2ufix64",
    "float2double"};
static const char *const SD_NAMES[32] = {
    "dadd", "dsub", "dmul", "ddiv", "dcmp_fast", "dcmp_fast_flags", "dsqrt", "double2int", "double2fix",
    "double2uint", "double2ufix", "int2double", "fix2double", "uint2double", "ufix2double", "dcos", "dsin",
    "dtan", "dsincos", "dexp", "dln", "dcmp_basic", "datan2", "int642double", "fix642double",
    "uint642double", "ufix642double", "double2int64", "double2fix64", "double2uint64", "double2ufix64",
    "double2float"};

// ---- the inputs: a xorshift64 generator, biased toward the edges ----

static uint64_t seed;
static uint64_t next(void) {
    uint64_t x = seed;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return seed = x;
}

static const uint32_t EDGE32[16] = {0, 1, 0x7FFFFF, 0x800000, 0x7F7FFFFF, 0x7F800000, 0x7FC00000, 0x7F800001,
    0x3F800000, 0x3F7FFFFF, 0x3F800001, 0x4B000000, 0x4F000000, 0x5F000000, 0x4EFFFFFF, 0x00800001};
static const uint64_t EDGE64[16] = {0, 1, 0xFFFFFFFFFFFFFull, 0x10000000000000ull, 0x7FEFFFFFFFFFFFFFull,
    0x7FF0000000000000ull, 0x7FF8000000000000ull, 0x7FF0000000000001ull, 0x3FF0000000000000ull,
    0x3FEFFFFFFFFFFFFFull, 0x3FF0000000000001ull, 0x41E0000000000000ull, 0x43E0000000000000ull,
    0x4330000000000000ull, 0x41EFFFFFFFE00000ull, 0x10000000000001ull};

static uint32_t edge32(void) {
    uint64_t v = next();
    uint32_t sign = (uint32_t)(v >> 63) << 31;
    uint32_t mant = (uint32_t)(v >> 8) & 0x7FFFFF;
    uint32_t e = (uint32_t)(v >> 40) & 0xFF;
    switch (v & 15) {
    case 0: return sign | EDGE32[(v >> 4) & 15];
    case 1: return sign | (((e & 3) + 1) << 23) | mant;
    case 2: return sign | ((251 + (e & 3)) << 23) | mant;
    case 3: return sign | mant;
    case 4: return sign | (e << 23) | ((v & 0x100000) ? 0x7FFFFF : 0);
    case 5: return sign | (e << 23) | (1u << ((v >> 24) % 23));
    case 6: return sign | ((120 + (e & 15)) << 23) | mant;
    default: return (uint32_t)v;
    }
}

static uint32_t partner32(uint32_t a) {
    uint64_t v = next();
    switch (v & 7) {
    case 0: return a + (uint32_t)((v >> 8) & 3);
    case 1: return (a ^ 0x80000000u) + (uint32_t)((v >> 8) & 3);
    case 2: return (a ^ ((uint32_t)(v >> 32) & 0x80000000u)) - (uint32_t)((v >> 8) & 3);
    case 3: {
        uint32_t e = (a >> 23) & 0xFF;
        if (e < 26) return edge32();
        return (a & 0x80000000u) | ((e - 24 - (uint32_t)((v >> 8) & 1)) << 23) | ((v & 0x200) ? 0x400000 : 0);
    }
    default: return edge32();
    }
}

static uint64_t edge64(void) {
    uint64_t v = next();
    uint64_t w = next();
    uint64_t sign = (v >> 63) << 63;
    uint64_t mant = w & 0xFFFFFFFFFFFFFull;
    uint64_t e = (v >> 40) & 0x7FF;
    switch (v & 15) {
    case 0: return sign | EDGE64[(v >> 4) & 15];
    case 1: return sign | (((e & 3) + 1) << 52) | mant;
    case 2: return sign | ((2043 + (e & 3)) << 52) | mant;
    case 3: return sign | mant;
    case 4: return sign | (e << 52) | ((v & 0x100000) ? 0xFFFFFFFFFFFFFull : 0);
    case 5: return sign | (e << 52) | (1ull << ((v >> 24) % 52));
    case 6: return sign | ((1016 + (e & 15)) << 52) | mant;
    default: return w;
    }
}

static uint64_t partner64(uint64_t a) {
    uint64_t v = next();
    switch (v & 7) {
    case 0: return a + ((v >> 8) & 3);
    case 1: return (a ^ 0x8000000000000000ull) + ((v >> 8) & 3);
    case 2: return (a ^ (v & 0x8000000000000000ull)) - ((v >> 8) & 3);
    case 3: {
        uint64_t e = (a >> 52) & 0x7FF;
        if (e < 56) return edge64();
        return (a & 0x8000000000000000ull) | ((e - 53 - ((v >> 8) & 1)) << 52) | ((v & 0x200) ? 0x8000000000000ull : 0);
    }
    default: return edge64();
    }
}

// ---- the CRC of our answers (CRC-32, as zlib's) ----

static uint32_t crc = 0xFFFFFFFF;
static void crc_word(uint32_t w) {
    for (int k = 0; k < 4; k++) {
        crc ^= (w >> (8 * k)) & 0xFF;
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
    }
}

// ---- calling a table entry: r0 to r3 in, r0 to r3 out ----

static void call4(uintptr_t fn, const uint32_t in[4], uint32_t out[4]) {
    register uint32_t r0 __asm__("r0") = in[0];
    register uint32_t r1 __asm__("r1") = in[1];
    register uint32_t r2 __asm__("r2") = in[2];
    register uint32_t r3 __asm__("r3") = in[3];
    register uint32_t r4 __asm__("r4") = fn;
    __asm__ volatile("blx r4" : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3) : "r"(r4) : "r12", "lr", "memory", "cc");
    out[0] = r0;
    out[1] = r1;
    out[2] = r2;
    out[3] = r3;
}

static uint32_t total, mismatches;

// One function's tally: how many cases, how many disagreed.
typedef struct {
    const char *name;
    uint32_t n, bad;
} tally_t;

static void report(tally_t *t) {
    printf("%s %lu pass %lu mismatch\n", t->name, (unsigned long)(t->n - t->bad), (unsigned long)t->bad);
    total += t->n;
    mismatches += t->bad;
}

// The registers a float entry takes for the floats `a`, `b`, the 64-bit `l` and the scale `n`, and how
// many registers of its answer count.
static int float_args(int i, uint32_t a, uint32_t b, uint64_t l, int32_t n, uint32_t in[4]) {
    in[0] = a; in[1] = 0; in[2] = 0; in[3] = 0;
    switch (i) {
    case 0: case 1: case 2: case 3: case 4: case 21: case 22: in[1] = b; return 1;
    case 8: case 10: case 12: case 14: in[1] = (uint32_t)n; return 1;
    case 16: case 18: return 2;
    case 23: case 25: in[0] = (uint32_t)l; in[1] = (uint32_t)(l >> 32); return 1;
    case 24: case 26: in[0] = (uint32_t)l; in[1] = (uint32_t)(l >> 32); in[2] = (uint32_t)n; return 1;
    case 27: case 29: case 31: return 2;
    case 28: case 30: in[1] = (uint32_t)n; return 2;
    default: return 1;
    }
}

// The same for a double entry.
static int double_args(int i, uint64_t a, uint64_t b, uint64_t l, int32_t n, uint32_t in[4]) {
    in[0] = (uint32_t)a; in[1] = (uint32_t)(a >> 32); in[2] = 0; in[3] = 0;
    switch (i) {
    case 0: case 1: case 2: case 3: case 22: in[2] = (uint32_t)b; in[3] = (uint32_t)(b >> 32); return 2;
    case 4: case 21: in[2] = (uint32_t)b; in[3] = (uint32_t)(b >> 32); return 1;
    case 7: case 9: case 31: return 1;
    case 8: case 10: in[2] = (uint32_t)n; return 1;
    case 11: case 13: in[0] = (uint32_t)l; in[1] = 0; return 2;
    case 12: case 14: in[0] = (uint32_t)l; in[1] = (uint32_t)n; return 2;
    case 18: return 4;
    case 23: case 25: in[0] = (uint32_t)l; in[1] = (uint32_t)(l >> 32); return 2;
    case 24: case 26: in[0] = (uint32_t)l; in[1] = (uint32_t)(l >> 32); in[2] = (uint32_t)n; return 2;
    case 28: case 30: in[2] = (uint32_t)n; return 2;
    default: return 2;
    }
}

static void check_entry(tally_t *t, uintptr_t chip, uintptr_t ours, const uint32_t in[4], int count) {
    uint32_t want[4], got[4];
    call4(chip, in, want);
    call4(ours, in, got);
    int same = 1;
    for (int k = 0; k < count; k++) {
        crc_word(got[k]);
        if (got[k] != want[k]) same = 0;
    }
    t->n++;
    if (!same) {
        t->bad++;
        if (t->bad <= 5) {
            printf("  %s(%08lx %08lx %08lx %08lx): chip", t->name, (unsigned long)in[0], (unsigned long)in[1],
                (unsigned long)in[2], (unsigned long)in[3]);
            for (int k = 0; k < count; k++) printf(" %08lx", (unsigned long)want[k]);
            printf(", ours");
            for (int k = 0; k < count; k++) printf(" %08lx", (unsigned long)got[k]);
            printf("\n");
        }
    }
}

static void check_tables(void) {
    const uint32_t *sf = (const uint32_t *)rom_data_lookup(ROM_TABLE_CODE('S', 'F'));
    const uint32_t *sd = (const uint32_t *)rom_data_lookup(ROM_TABLE_CODE('S', 'D'));
    for (int i = 0; i < 32; i++) {
        // The comparison that answers in the flags is not a function's result.
        if (i == 5) continue;
        tally_t tf = {SF_NAMES[i], 0, 0};
        tally_t td = {SD_NAMES[i], 0, 0};
        seed = 0x9E3779B97F4A7C15ull + (uint64_t)i;
        for (int k = 0; k < 16 + RANDOM; k++) {
            uint32_t a = k < 16 ? EDGE32[k] : edge32();
            uint32_t b = partner32(a);
            uint64_t l = next() >> (next() % 64);
            int32_t n = (int32_t)(next() % 65) - 32;
            uint32_t in[4];
            int count = float_args(i, a, b, l, n, in);
            check_entry(&tf, sf[i], ours_sf_table[i], in, count);
            uint64_t x = k < 16 ? EDGE64[k] : edge64();
            count = double_args(i, x, partner64(x), l, n, in);
            check_entry(&td, sd[i], ours_sd_table[i], in, count);
        }
        report(&tf);
        report(&td);
    }
}

typedef uint32_t (*bits_fn)(uint32_t);

static void check_bits(void) {
    static const struct { const char *name; uint32_t code; bits_fn ours; } fns[4] = {
        {"popcount32", ROM_FUNC_POPCOUNT32, ours_popcount32}, {"reverse32", ROM_FUNC_REVERSE32, ours_reverse32},
        {"clz32", ROM_FUNC_CLZ32, ours_clz32}, {"ctz32", ROM_FUNC_CTZ32, ours_ctz32}};
    for (int f = 0; f < 4; f++) {
        bits_fn chip = (bits_fn)rom_func_lookup(fns[f].code);
        tally_t t = {fns[f].name, 0, 0};
        seed = 0xD1B54A32D192ED03ull + (uint64_t)f;
        for (int k = 0; k < 64 + RANDOM; k++) {
            uint32_t x = k < 32 ? (1u << k) : k < 64 ? ~(1u << (k - 32)) : (uint32_t)(next() >> (next() % 32));
            uint32_t in[4] = {x, 0, 0, 0};
            check_entry(&t, (uintptr_t)chip, (uintptr_t)fns[f].ours, in, 1);
        }
        report(&t);
    }
}

typedef uint8_t *(*set_fn)(uint8_t *, uint8_t, uint32_t);
typedef uint8_t *(*copy_fn)(uint8_t *, const uint8_t *, uint32_t);

static uint8_t chip_buf[128] __attribute__((aligned(4)));
static uint8_t ours_buf[128] __attribute__((aligned(4)));
static uint8_t source[128] __attribute__((aligned(4)));

static void check_mem(void) {
    static const struct { const char *name; uint32_t code; int copy; int aligned; void *ours; } fns[4] = {
        {"memset", ROM_FUNC_MEMSET, 0, 0, (void *)ours_memset}, {"memset4", ROM_FUNC_MEMSET4, 0, 1, (void *)ours_memset4},
        {"memcpy", ROM_FUNC_MEMCPY, 1, 0, (void *)ours_memcpy}, {"memcpy44", ROM_FUNC_MEMCPY44, 1, 1, (void *)ours_memcpy44}};
    for (int i = 0; i < 128; i++) source[i] = (uint8_t)(i * 37 + 11);
    for (int f = 0; f < 4; f++) {
        void *chip = rom_func_lookup(fns[f].code);
        tally_t t = {fns[f].name, 0, 0};
        seed = 0x2545F4914F6CDD1Dull + (uint64_t)f;
        for (int k = 0; k < RANDOM; k++) {
            uint32_t to = (uint32_t)(next() % 32), from = (uint32_t)(next() % 32), n = (uint32_t)(next() % 65);
            uint8_t c = (uint8_t)next();
            if (fns[f].aligned) {
                to &= ~3u;
                from &= ~3u;
                n &= ~3u;
            }
            for (int i = 0; i < 128; i++) chip_buf[i] = ours_buf[i] = (uint8_t)(i ^ 0x5A);
            uint8_t *rc, *ro;
            if (fns[f].copy) {
                rc = ((copy_fn)chip)(chip_buf + to, source + from, n);
                ro = ((copy_fn)fns[f].ours)(ours_buf + to, source + from, n);
            } else {
                rc = ((set_fn)chip)(chip_buf + to, c, n);
                ro = ((set_fn)fns[f].ours)(ours_buf + to, c, n);
            }
            int same = memcmp(chip_buf, ours_buf, sizeof chip_buf) == 0 && rc - chip_buf == ro - ours_buf;
            for (int i = 0; i < 128; i += 4) crc_word(ours_buf[i] | ours_buf[i + 1] << 8 | ours_buf[i + 2] << 16 | (uint32_t)ours_buf[i + 3] << 24);
            t.n++;
            if (!same && ++t.bad <= 5)
                printf("  %s(+%lu, +%lu or %02x, %lu): the buffers or the answers differ\n", t.name, (unsigned long)to,
                    (unsigned long)from, c, (unsigned long)n);
        }
        report(&t);
    }
}

// benchtop's ROM names itself in its copyright text; Raspberry Pi's does not.
static bool on_benchtop(void) {
    const char *cr = (const char *)rom_data_lookup(ROM_DATA_COPYRIGHT);
    return cr && strncmp(cr, "benchtop", 8) == 0;
}

int main(void) {
    stdio_uart_init();
    bool benchtop = on_benchtop();
    if (!benchtop) {
        // benchtop has no USB controller; on silicon, wait for the host to open the port.
        stdio_usb_init();
        while (!stdio_usb_connected()) sleep_ms(100);
        sleep_ms(500);
    }
    printf("rom_crosscheck: the chip's ROM, version %d, against benchtop's ROM routines\n", rp2040_rom_version());
    check_tables();
    check_bits();
    check_mem();
    printf("CROSSCHECK %lu %lu %08lx\n", (unsigned long)total, (unsigned long)mismatches, (unsigned long)~crc);
    if (benchtop) return 0;
    while (true) {
        sleep_ms(5000);
        printf("CROSSCHECK %lu %lu %08lx\n", (unsigned long)total, (unsigned long)mismatches, (unsigned long)~crc);
    }
}
