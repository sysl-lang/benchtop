// Echoes what is typed from the console's interrupt handler while main sleeps in WFI, until the input
// ends or Ctrl-D, then counts the bytes echoed.

#include "virt.h"

static volatile int done;
static volatile unsigned echoed;

// IRQ 0: raised while STATUS is not 0, so it reads until nothing is waiting, and turns the interrupt
// off once the input has ended.
void console_handler(void) {
    for (;;) {
        unsigned status = CONSOLE_STATUS;

        if (status & CONSOLE_WAITING) {
            int c = (int)CONSOLE_DATA;

            if (c == 4) break;
            if (c == '\r') c = '\n';

            put_char((char)c);
            echoed++;
            continue;
        }

        if (status & CONSOLE_ENDED) break;
        return;
    }

    done = 1;
    CONSOLE_CTRL = 0;
}

int main(void) {
    put_string("echoing from an interrupt; Ctrl-D ends\n");

    NVIC_ISER = 1u << CONSOLE_IRQ;
    CONSOLE_CTRL = CONSOLE_RX_INTERRUPT;

    // Interrupts stay masked while `done` is tested, so one cannot land between the test and the
    // WFI; a pending one still wakes the WFI, and is taken at the CPSIE after it.
    disable_interrupts();

    while (!done) {
        wait_for_interrupt();
        enable_interrupts();
        disable_interrupts();
    }

    enable_interrupts();

    put_string("\nbye after ");
    put_number(echoed);
    put_string(" bytes\n");
    return 0;
}
