// The vector table and reset handler for the virtual board: zero .bss, run main, and end with what
// it answers. Code and data are both loaded into RAM where they run, so nothing is copied.
//
// A program handles an exception by defining the function its entry names: `svc_handler`,
// `pendsv_handler`, `systick_handler`, `console_handler` (IRQ 0) and so on. An entry no program
// defines is 0, and cortical stops at that exception with a fault on the vector's address.

#include "virt.h"

extern unsigned __bss_start[], __bss_end[], __stack_top[];

int main(void);

void nmi_handler(void) __attribute__((weak));
void hard_fault_handler(void) __attribute__((weak));
void svc_handler(void) __attribute__((weak));
void pendsv_handler(void) __attribute__((weak));
void systick_handler(void) __attribute__((weak));
void console_handler(void) __attribute__((weak));

void __attribute__((noreturn)) reset(void) {
    for (unsigned *p = __bss_start; p < __bss_end; p++) *p = 0;
    finish(main());
}

// The sixteen system exceptions, then the 32 IRQs.
__attribute__((section(".vectors"), used)) const void *const vectors[48] = {
    [0] = __stack_top,
    [1] = (const void *)reset,
    [2] = (const void *)nmi_handler,
    [3] = (const void *)hard_fault_handler,
    [11] = (const void *)svc_handler,
    [14] = (const void *)pendsv_handler,
    [15] = (const void *)systick_handler,
    [16 + CONSOLE_IRQ] = (const void *)console_handler,
};
