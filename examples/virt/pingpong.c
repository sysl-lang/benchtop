// The FIFO under load: core 0 streams the numbers 1 to COUNT to core 1, pushing whenever there is
// room and popping whenever a reply waits, so both FIFOs run full. Core 1 sleeps in WFI and answers
// from its FIFO interrupt, sending back each number plus REPLY. Core 0 checks that every reply comes,
// in order, with none lost and no FIFO error set, prints what it saw, and exits 0 when all is well.

#include "virt.h"

#define COUNT 20000
#define REPLY 1000000

static unsigned core1_stack[256] __attribute__((aligned(8)));

// IRQ 1, which core 1 alone enables: answer every number waiting.
void sio_handler(void) {
    while (SIO_FIFO_ST & SIO_FIFO_VLD) {
        unsigned n = SIO_FIFO_RD;

        fifo_push(n + REPLY);
    }
}

static void core1_main(void) {
    NVIC_ISER = 1u << SIO_IRQ;

    for (;;) wait_for_interrupt();
}

int main(void) {
    launch_core1(core1_main, &core1_stack[256]);

    unsigned sent = 0, got = 0, wrong = 0;

    while (got < COUNT) {
        unsigned st = SIO_FIFO_ST;

        if (st & SIO_FIFO_VLD) {
            if (SIO_FIFO_RD != got + 1 + REPLY) wrong++;
            got++;
        }

        if (sent < COUNT && (st & SIO_FIFO_RDY)) SIO_FIFO_WR = ++sent;
    }

    unsigned errors = SIO_FIFO_ST & (SIO_FIFO_WOF | SIO_FIFO_ROE);

    put_string("replies ");
    put_number(got);
    put_string(", wrong ");
    put_number(wrong);
    put_string(", errors ");
    put_number(errors);
    put_char('\n');
    return wrong || errors ? 1 : 0;
}
