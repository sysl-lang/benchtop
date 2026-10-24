// The vector table and reset handler for the virtual board: zero .bss, run main, and end with what
// it answers. Code and data are both loaded into RAM where they run, so nothing is copied.

#include "virt.h"

extern unsigned __bss_start[], __bss_end[], __stack_top[];

int main(void);

void __attribute__((noreturn)) reset(void) {
    for (unsigned *p = __bss_start; p < __bss_end; p++) *p = 0;
    finish(main());
}

__attribute__((section(".vectors"), used)) const void *const vectors[] = {
    __stack_top,
    (const void *)reset,
};
