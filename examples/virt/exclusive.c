// Both cores add to one counter ROUNDS times each with no lock, the additions atomic through the
// exclusives: a word with LDREX/STREX, a byte and a halfword with LDREXB/STREXB and LDREXH/STREXH
// (C's __atomic_fetch_add, which GCC makes a load-exclusive, store-exclusive loop for an M33). Core 0
// prints the totals once core 1 says through the FIFO that it has finished. The exclusives exclude
// when every total is exact, and the exit status is 0 then and 1 otherwise.
//
// For the M33 only: an M0+ has no exclusives.

#include "virt.h"

#define ROUNDS   20000
#define FINISHED 0xD0E

static unsigned core1_stack[256] __attribute__((aligned(8)));
static volatile unsigned word;
static volatile unsigned short half;
static volatile unsigned char byte;

static void add(void) {
    for (unsigned i = 0; i < ROUNDS; i++) {
        __atomic_fetch_add(&word, 1, __ATOMIC_RELAXED);
        __atomic_fetch_add(&half, 1, __ATOMIC_RELAXED);
        __atomic_fetch_add(&byte, 1, __ATOMIC_SEQ_CST);
    }
}

static void core1_main(void) {
    add();
    fifo_push(FINISHED);

    for (;;) wait_for_interrupt();
}

int main(void) {
    launch_core1(core1_main, &core1_stack[256]);
    add();

    while (fifo_pop() != FINISHED) {
    }

    put_string("word ");
    put_number(word);
    put_string(", half ");
    put_number(half);
    put_string(", byte ");
    put_number(byte);
    put_char('\n');

    unsigned total = 2 * ROUNDS;

    return word == total && half == (total & 0xFFFF) && byte == (total & 0xFF) ? 0 : 1;
}
