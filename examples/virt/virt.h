// The virtual board's debug console (sh.sysl.cortical.virt), and the start-up every program shares.

#ifndef VIRT_H
#define VIRT_H

#define CONSOLE_DATA   (*(volatile unsigned *)0x40000000)
#define CONSOLE_STATUS (*(volatile unsigned *)0x40000004)
#define CONSOLE_EXIT   (*(volatile unsigned *)0x40000008)
#define CONSOLE_CTRL   (*(volatile unsigned *)0x4000000C)

#define CONSOLE_WAITING 1u
#define CONSOLE_ENDED   2u

// CONSOLE_CTRL's bit: raise IRQ 0 while STATUS is not 0.
#define CONSOLE_RX_INTERRUPT 1u
#define CONSOLE_IRQ          0

// The NVIC's set-enable register: bit n enables IRQ n.
#define NVIC_ISER (*(volatile unsigned *)0xE000E100)

static inline void put_char(char c) { CONSOLE_DATA = (unsigned char)c; }

static inline void put_string(const char *s) {
    while (*s) put_char(*s++);
}

static inline void put_number(unsigned n) {
    char digits[10];
    int k = 0;

    do {
        digits[k++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);

    while (k) put_char(digits[--k]);
}

// The next byte typed, waiting for one; -1 once the input has ended.
static inline int get_char(void) {
    for (;;) {
        unsigned status = CONSOLE_STATUS;

        if (status & CONSOLE_WAITING) return (int)CONSOLE_DATA;
        if (status & CONSOLE_ENDED) return -1;
    }
}

static inline void disable_interrupts(void) { __asm__ volatile("cpsid i" ::: "memory"); }

static inline void enable_interrupts(void) { __asm__ volatile("cpsie i" ::: "memory"); }

static inline void wait_for_interrupt(void) { __asm__ volatile("wfi" ::: "memory"); }

static inline void __attribute__((noreturn)) finish(int code) {
    CONSOLE_EXIT = (unsigned)code;
    for (;;) {
    }
}

#endif
