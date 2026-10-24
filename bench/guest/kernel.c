// A guest workload for the dispatch benchmark: integer loops of the kinds firmware runs — a bitwise
// CRC, a sieve, an insertion sort over a pseudo-random array, and a call-heavy recursion. Built for
// ARMv6-M, so every instruction is one the M0+ decoder must already handle.

typedef unsigned int u32;
typedef unsigned char u8;

static u8 buf[4096];
static u8 sieve[8192];
static u32 arr[256];

static u32 crc32(const u8 *p, u32 n) {
    u32 c = 0xFFFFFFFFu;
    for (u32 i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
    }
    return ~c;
}

static u32 primes(void) {
    u32 count = 0;
    for (u32 i = 0; i < sizeof sieve; i++) sieve[i] = 1;
    for (u32 i = 2; i < sizeof sieve; i++) {
        if (!sieve[i]) continue;
        count++;
        for (u32 j = i + i; j < sizeof sieve; j += i) sieve[j] = 0;
    }
    return count;
}

static u32 sort(u32 seed) {
    for (u32 i = 0; i < 256; i++) { seed = seed * 1103515245u + 12345u; arr[i] = seed >> 8; }
    for (u32 i = 1; i < 256; i++) {
        u32 v = arr[i], j = i;
        while (j > 0 && arr[j - 1] > v) { arr[j] = arr[j - 1]; j--; }
        arr[j] = v;
    }
    return arr[0] ^ arr[128] ^ arr[255];
}

__attribute__((noinline)) static u32 fib(u32 n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

u32 run(u32 rounds) {
    u32 acc = 0;
    for (u32 i = 0; i < sizeof buf; i++) buf[i] = (u8)(i * 7 + 3);
    for (u32 r = 0; r < rounds; r++) {
        acc += crc32(buf, sizeof buf);
        acc ^= primes();
        acc += sort(acc + r);
        acc ^= fib(18);
    }
    return acc;
}
