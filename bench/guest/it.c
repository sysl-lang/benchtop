// What `it.s` computes, on the host, for `benchtop bench`'s expected result:
// `cc -O2 bench/guest/it.c -o it && ./it` prints the answer for 1000 rounds, `./it <n>` for n.

#include <stdio.h>
#include <stdlib.h>

typedef unsigned int u32;

static u32 ror(u32 x, int k) { return x >> k | x << (32 - k); }

int main(int argc, char **argv) {
    u32 rounds = argc > 1 ? (u32)atoi(argv[1]) : 1000;
    u32 x = 12345, acc = 0, lo = 0, hi = 0;

    for (u32 r = 0; r < rounds; r++) {
        for (u32 i = 0; i < 10000; i++) {
            x = x * 1103515245u + 12345u;
            u32 v = x >> 16;

            if (v < lo) lo = v;
            if (v > hi) hi = v; else acc += 1;
            if (x & 0x100) { acc += v; acc ^= ror(x, 7); lo += 1; }
            if (!(x & 0x8000)) { acc -= v; hi ^= 0x55; } else { acc += v; acc = ror(acc, 3); }
            if (v >= 0x8000 && acc >= lo) lo += 1;
        }
    }
    printf("%u\n", acc ^ lo ^ hi);
    return 0;
}
