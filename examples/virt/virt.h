// The virtual board's debug console (sh.sysl.cortical.virt), and the start-up every program shares.

#ifndef VIRT_H
#define VIRT_H

#define CONSOLE_DATA   (*(volatile unsigned *)0x40000000)
#define CONSOLE_STATUS (*(volatile unsigned *)0x40000004)
#define CONSOLE_EXIT   (*(volatile unsigned *)0x40000008)

#define CONSOLE_WAITING 1u
#define CONSOLE_ENDED   2u

static inline void put_char(char c) { CONSOLE_DATA = (unsigned char)c; }

static inline void put_string(const char *s) {
    while (*s) put_char(*s++);
}

// The next byte typed, waiting for one; -1 once the input has ended.
static inline int get_char(void) {
    for (;;) {
        unsigned status = CONSOLE_STATUS;

        if (status & CONSOLE_WAITING) return (int)CONSOLE_DATA;
        if (status & CONSOLE_ENDED) return -1;
    }
}

static inline void __attribute__((noreturn)) finish(int code) {
    CONSOLE_EXIT = (unsigned)code;
    for (;;) {
    }
}

#endif
