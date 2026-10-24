// A two-core workload for the virtual board: each core runs the bitwise CRC over a buffer of its own,
// ROUNDS times, with no sharing at all while it does; then core 1 sends its result to core 0 through
// the FIFO, and core 0 prints both and exits 0. The work is the same on one host thread and on two,
// so the time it takes compares them.

#include "virt.h"

#define ROUNDS 1000
#define BYTES  4096

typedef unsigned int u32;
typedef unsigned char u8;

static unsigned core1_stack[256] __attribute__((aligned(8)));
static u8 data[2][BYTES];

static u32 crc32(const u8 *p, u32 n) {
    u32 c = 0xFFFFFFFFu;

    for (u32 i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
    }

    return ~c;
}

// Core k's share: fill its buffer, then fold its CRC ROUNDS times.
static u32 work(u32 k) {
    u8 *buf = data[k];
    u32 acc = 0;

    for (u32 i = 0; i < BYTES; i++) buf[i] = (u8)(i * 7 + 3 + k);

    for (u32 r = 0; r < ROUNDS; r++) {
        acc += crc32(buf, BYTES);
        buf[r % BYTES] ^= (u8)acc;
    }

    return acc;
}

static void core1_main(void) {
    fifo_push(work(1));

    for (;;) wait_for_interrupt();
}

int main(void) {
    launch_core1(core1_main, &core1_stack[256]);

    u32 mine = work(0);
    u32 theirs = fifo_pop();

    put_string("core 0 ");
    put_number(mine);
    put_string(", core 1 ");
    put_number(theirs);
    put_char('\n');
    return 0;
}
