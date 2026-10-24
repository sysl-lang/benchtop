// Message passing through RAM, ordered by barriers alone. Core 0 writes each number to a slot of a
// ring and then, after a DMB, publishes it as the newest. Core 1 reads the newest and then, after a
// DMB, its slot, which must hold that number, or a later one written over it, and never an earlier.
// Core 1 sends back how many earlier ones it saw; core 0 prints it and exits 0 when it is none.

#include "virt.h"

#define SLOTS 64
#define COUNT 200000

static unsigned core1_stack[256] __attribute__((aligned(8)));
static volatile unsigned ring[SLOTS];
static volatile unsigned newest;

static inline void dmb(void) { __asm__ volatile("dmb" ::: "memory"); }

static void core1_main(void) {
    unsigned stale = 0, looks = 0, last = 0;

    while (last != COUNT) {
        last = newest;
        dmb();

        unsigned v = ring[last % SLOTS];

        if (last && v < last) stale++;
        looks++;
    }

    fifo_push(stale);
    fifo_push(looks);

    for (;;) wait_for_interrupt();
}

int main(void) {
    launch_core1(core1_main, &core1_stack[256]);

    for (unsigned i = 1; i <= COUNT; i++) {
        ring[i % SLOTS] = i;
        dmb();
        newest = i;
    }

    unsigned stale = fifo_pop();
    unsigned looks = fifo_pop();

    put_string("stale ");
    put_number(stale);
    put_string(" of ");
    put_number(looks);
    put_char('\n');
    return stale ? 1 : 0;
}
