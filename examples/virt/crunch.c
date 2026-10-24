// Arithmetic a compiler turns into most of the integer instruction set: 64-bit multiplies, divides
// and shifts, signed and unsigned division, bitfields, byte reversals, leading zeros, switch tables,
// saturation, conditional selection and an atomic counter, each result printed in hex on a line.
//
// The same source built for the host (`cc -DHOST crunch.c`) prints the same lines, and that output,
// crunch.out, is what a build for either core is checked against (build.sh writes both).

#include <stdint.h>

#ifdef HOST
#include <stdio.h>

static void put_char(char c) { putchar(c); }
#else
#include "virt.h"
#endif

static void put_text(const char *s) {
    while (*s) put_char(*s++);
}

static void put_hex(uint32_t x) {
    for (int k = 28; k >= 0; k -= 4) put_char("0123456789abcdef"[(x >> k) & 0xF]);
}

static void line(const char *name, uint32_t x) {
    put_text(name);
    put_text(" ");
    put_hex(x);
    put_text("\n");
}

static void line64(const char *name, uint64_t x) {
    put_text(name);
    put_text(" ");
    put_hex((uint32_t)(x >> 32));
    put_hex((uint32_t)x);
    put_text("\n");
}

// A pseudo-random sequence, the same everywhere.
static uint32_t seed = 12345;

static uint32_t next(void) {
    seed = seed * 1103515245u + 12345u;
    return seed;
}

static uint32_t crc32(const uint8_t *p, int n) {
    uint32_t crc = 0xFFFFFFFFu;

    for (int i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
    }

    return ~crc;
}

struct packed {
    uint32_t low : 5;
    uint32_t mid : 11;
    int32_t high : 9;
    uint32_t top : 7;
};

static uint32_t classify(uint32_t x) {
    switch (x % 12) {
    case 0: return x + 1;
    case 1: return x ^ 0x55;
    case 2: return x << 3;
    case 3: return x >> 2;
    case 4: return x * 7;
    case 5: return ~x;
    case 6: return x - 100;
    case 7: return x | 0x8000;
    case 8: return x & 0xFF;
    case 9: return x + x / 3;
    case 10: return (x << 16) | (x >> 16);
    default: return 0;
    }
}

static int32_t clamp8(int32_t x) { return x < -128 ? -128 : x > 127 ? 127 : x; }

static uint32_t clampu10(int32_t x) { return x < 0 ? 0 : x > 1023 ? 1023 : (uint32_t)x; }

static int32_t fix_mul(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }

static uint32_t ackermann(uint32_t m, uint32_t n) {
    if (m == 0) return n + 1;
    if (n == 0) return ackermann(m - 1, 1);
    return ackermann(m - 1, ackermann(m, n - 1));
}

static volatile uint32_t counter;

static void count_up(uint32_t by) {
#ifdef __ARM_FEATURE_LDREX
    __atomic_fetch_add(&counter, by, __ATOMIC_SEQ_CST);
#else
    counter += by;
#endif
}

static uint8_t buffer[512];
static int16_t shorts[64];

int main(void) {
    for (int i = 0; i < 512; i++) buffer[i] = (uint8_t)next();
    line("crc", crc32(buffer, 512));

    uint64_t acc = 0;
    for (int i = 0; i < 100; i++) acc = acc * 6364136223846793005ull + next();
    line64("lcg64", acc);
    line64("div64", acc / 1000003u);
    line64("mod64", acc % 1000003u);
    line64("sdiv64", (uint64_t)((int64_t)acc / -7));
    line64("shift64", (acc << 13) ^ (acc >> 51) ^ (uint64_t)((int64_t)acc >> 29));

    uint32_t sum = 0;
    for (int i = 0; i < 200; i++) {
        int32_t a = (int32_t)next();
        int32_t b = (int32_t)(next() >> (i % 20)) - 1000;
        if (b == 0 || (b == -1 && a == INT32_MIN)) b = 3;
        sum += (uint32_t)(a / b) + (uint32_t)(a % b) + (uint32_t)a / (uint32_t)b;
    }
    line("division", sum);

    struct packed p = {0};
    uint32_t bits = 0;
    for (int i = 0; i < 100; i++) {
        uint32_t r = next();
        p.low = r;
        p.mid += r >> 7;
        p.high = (int32_t)(r >> 3) - p.high;
        p.top ^= r >> 25;
        bits = bits * 31 + p.low + p.mid + (uint32_t)p.high + p.top;
    }
    line("bitfields", bits);

    uint32_t mixed = 0;
    for (int i = 0; i < 100; i++) {
        uint32_t r = next() >> (i % 32);
        mixed += (uint32_t)__builtin_clz(r | 1) + __builtin_bswap32(r) + (uint32_t)__builtin_popcount(r);
        mixed ^= (uint32_t)__builtin_ctz(r | 0x80000000u) << (i % 7);
    }
    line("bits", mixed);

    uint32_t table = 0;
    for (uint32_t i = 0; i < 300; i++) table = table * 3 + classify(i * 2654435761u);
    line("switch", table);

    uint32_t sat = 0;
    for (int i = 0; i < 300; i++) {
        int32_t v = (int32_t)next() >> (i % 24);
        sat = sat * 5 + (uint32_t)clamp8(v) + clampu10(v);
    }
    line("saturate", sat);

    int32_t fx = 0x10000;
    for (int i = 1; i < 40; i++) fx = (int32_t)((uint32_t)fix_mul(fx, 0x10000 + (i << 10)) - (uint32_t)(i << 8));
    line("fixed", (uint32_t)fx);

    for (int i = 0; i < 64; i++) shorts[i] = (int16_t)next();
    for (int i = 1; i < 64; i++) {
        int16_t v = shorts[i];
        int j = i - 1;
        while (j >= 0 && shorts[j] > v) {
            shorts[j + 1] = shorts[j];
            j--;
        }
        shorts[j + 1] = v;
    }
    uint32_t sorted = 0;
    for (int i = 0; i < 64; i++) sorted = sorted * 17 + (uint32_t)shorts[i];
    line("sort", sorted);

    uint32_t select = 0;
    for (int i = 0; i < 500; i++) {
        uint32_t a = next(), b = next();
        uint32_t m = a < b ? a : b;
        int32_t s = (int32_t)a > (int32_t)b ? (int32_t)a : (int32_t)b;
        select += m ^ (uint32_t)s ^ (a == b ? 1u : 0u) ^ ((a & 1) ? b : a >> 3);
    }
    line("select", select);

    line("ackermann", ackermann(2, 300));

    for (uint32_t i = 0; i < 1000; i++) count_up(i);
    line("atomic", counter);

    return 0;
}
