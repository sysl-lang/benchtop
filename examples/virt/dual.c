// Both cores: core 0 launches core 1, sends it numbers through the FIFO and prints the squares core 1
// sends back from its FIFO interrupt; then each prints a few lines of its own at once, a line at a
// time under spinlock 0, and core 1 says it has finished through the FIFO before core 0 ends.

#include "virt.h"

#define CONSOLE_LOCK 0
#define FINISHED     0xD0E

static unsigned core1_stack[256] __attribute__((aligned(8)));
static volatile int core1_told_to_stop;

// One whole line on the console, so the other core's lines never break into it.
static void say(const char *what, unsigned n) {
    spin_lock(CONSOLE_LOCK);
    put_string("core ");
    put_number(SIO_CPUID);
    put_string(": ");
    put_string(what);
    put_number(n);
    put_char('\n');
    spin_unlock(CONSOLE_LOCK);
}

// A few lines, with some work between them, while the other core does the same.
static void chatter(void) {
    for (unsigned line = 1; line <= 3; line++) {
        say("line ", line);

        for (volatile unsigned spin = 0; spin < 200; spin++) {
        }
    }
}

// IRQ 1, which core 1 alone enables: square every number waiting and send it back; a 0 says stop.
void sio_handler(void) {
    while (SIO_FIFO_ST & SIO_FIFO_VLD) {
        unsigned n = SIO_FIFO_RD;

        if (n == 0) {
            core1_told_to_stop = 1;
            continue;
        }

        say("squaring ", n);
        SIO_FIFO_WR = n * n;
    }
}

static void core1_main(void) {
    say("up, with stack top at ", (unsigned)&core1_stack[256]);

    NVIC_ISER = 1u << SIO_IRQ;
    disable_interrupts();

    while (!core1_told_to_stop) {
        wait_for_interrupt();
        enable_interrupts();
        disable_interrupts();
    }

    enable_interrupts();
    chatter();
    fifo_push(FINISHED);

    for (;;) wait_for_interrupt();
}

int main(void) {
    say("launching core ", 1);
    launch_core1(core1_main, &core1_stack[256]);

    unsigned total = 0;

    for (unsigned n = 1; n <= 4; n++) {
        fifo_push(n);

        unsigned square = fifo_pop();

        say("got back ", square);
        total += square;
    }

    fifo_push(0);
    chatter();

    while (fifo_pop() != FINISHED) {
    }

    say("core 1 finished; total ", total);
    return 0;
}
