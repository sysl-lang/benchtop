// Both cores add 1 to one counter ROUNDS times each, a spinlock held around every addition, and core
// 0 prints the total once core 1 says through the FIFO that it has finished. The lock excludes when
// the total is exactly twice ROUNDS, and the exit status is 0 then and 1 otherwise. The barriers
// after a claim and before a release are the ones the RP2040 SDK's spin locks have.

#include "virt.h"

#define ROUNDS     20000
#define COUNT_LOCK 1
#define FINISHED   0xD0E

static unsigned core1_stack[256] __attribute__((aligned(8)));
static volatile unsigned counter;

static void count(void) {
    for (unsigned i = 0; i < ROUNDS; i++) {
        spin_lock(COUNT_LOCK);
        __asm__ volatile("dmb" ::: "memory");
        counter = counter + 1;
        __asm__ volatile("dmb" ::: "memory");
        spin_unlock(COUNT_LOCK);
    }
}

static void core1_main(void) {
    count();
    fifo_push(FINISHED);

    for (;;) wait_for_interrupt();
}

int main(void) {
    launch_core1(core1_main, &core1_stack[256]);
    count();

    while (fifo_pop() != FINISHED) {
    }

    put_string("total ");
    put_number(counter);
    put_char('\n');
    return counter == 2 * ROUNDS ? 0 : 1;
}
