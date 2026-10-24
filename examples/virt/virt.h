// The virtual board's debug console (sh.sysl.cortical.virt), and the start-up every program shares.

#ifndef VIRT_H
#define VIRT_H

// Where boards/virt.hocon puts the console and the SIO, and the interrupts they raise. A program for a
// board that moves them is built with these defined, as `-DCONSOLE_BASE=0x50000000 -DCONSOLE_IRQ=7`.
#ifndef CONSOLE_BASE
#define CONSOLE_BASE 0x40000000
#endif
#ifndef CONSOLE_IRQ
#define CONSOLE_IRQ 0
#endif
#ifndef SIO_BASE
#define SIO_BASE 0xD0000000
#endif
#ifndef SIO_IRQ
#define SIO_IRQ 1
#endif

#define CONSOLE_DATA   (*(volatile unsigned *)(CONSOLE_BASE + 0x0))
#define CONSOLE_STATUS (*(volatile unsigned *)(CONSOLE_BASE + 0x4))
#define CONSOLE_EXIT   (*(volatile unsigned *)(CONSOLE_BASE + 0x8))
#define CONSOLE_CTRL   (*(volatile unsigned *)(CONSOLE_BASE + 0xC))

#define CONSOLE_WAITING 1u
#define CONSOLE_ENDED   2u

// CONSOLE_CTRL's bit: raise CONSOLE_IRQ while STATUS is not 0.
#define CONSOLE_RX_INTERRUPT 1u

// The NVIC's set-enable register: bit n enables IRQ n.
#define NVIC_ISER (*(volatile unsigned *)0xE000E100)

// The vector table's address.
#define SCB_VTOR (*(volatile unsigned *)0xE000ED08)

// The SIO, shared by the two cores and answering each by which one asks: CPUID, the inter-core FIFOs
// (8 words each way) and 32 spinlocks, laid out as the RP2040's.
#define SIO_CPUID       (*(volatile unsigned *)(SIO_BASE + 0x00))
#define SIO_FIFO_ST     (*(volatile unsigned *)(SIO_BASE + 0x50))
#define SIO_FIFO_WR     (*(volatile unsigned *)(SIO_BASE + 0x54))
#define SIO_FIFO_RD     (*(volatile unsigned *)(SIO_BASE + 0x58))
#define SIO_SPINLOCK_ST (*(volatile unsigned *)(SIO_BASE + 0x5C))
#define SIO_SPINLOCK(k) (*(volatile unsigned *)(SIO_BASE + 0x100 + 4 * (k)))

// FIFO_ST's bits: this core's RX FIFO holds a word; its TX FIFO has room; it wrote to a full TX
// FIFO; it read an empty RX FIFO.
#define SIO_FIFO_VLD 1u
#define SIO_FIFO_RDY 2u
#define SIO_FIFO_WOF 4u
#define SIO_FIFO_ROE 8u

// SIO_IRQ is raised in a core's own NVIC while its VLD, WOF or ROE is set.

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

// Push a word to the other core, waiting for room; pop one from it, waiting for one; and throw away
// whatever is waiting.
static inline void fifo_push(unsigned x) {
    while (!(SIO_FIFO_ST & SIO_FIFO_RDY)) {
    }
    SIO_FIFO_WR = x;
}

static inline unsigned fifo_pop(void) {
    while (!(SIO_FIFO_ST & SIO_FIFO_VLD)) {
    }
    return SIO_FIFO_RD;
}

static inline void fifo_drain(void) {
    while (SIO_FIFO_ST & SIO_FIFO_VLD) (void)SIO_FIFO_RD;
}

// Claim spinlock k, waiting while the other core holds it; and release it.
static inline void spin_lock(unsigned k) {
    while (!SIO_SPINLOCK(k)) {
    }
}

static inline void spin_unlock(unsigned k) { SIO_SPINLOCK(k) = 1; }

// Start core 1 at `entry` on the stack `sp`, with core 0's vector table: the RP2040's launch sequence,
// each word echoed back by core 1's boot ROM, starting again on a wrong echo.
static inline void launch_core1(void (*entry)(void), unsigned *sp) {
    const unsigned sequence[6] = {0, 0, 1, SCB_VTOR, (unsigned)sp, (unsigned)entry};
    unsigned i = 0;

    while (i < 6) {
        unsigned word = sequence[i];

        if (!word) {
            fifo_drain();
            __asm__ volatile("sev");
        }

        fifo_push(word);
        i = fifo_pop() == word ? i + 1 : 0;
    }
}

static inline void __attribute__((noreturn)) finish(int code) {
    CONSOLE_EXIT = (unsigned)code;
    for (;;) {
    }
}

#endif
