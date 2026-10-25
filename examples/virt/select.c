// Armv8.1-M conditional selects: 16 x 16 edge operands and 12 NZCV states per case.
#include <stdint.h>

#ifdef SEMIHOSTING
// The semihosting calls QEMU answers: SYS_WRITEC and SYS_EXIT.
static void put_char(char c) {
    register uint32_t op __asm__("r0") = 3;
    register const char *arg __asm__("r1") = &c;

    __asm__ volatile("bkpt 0xab" : "+r"(op) : "r"(arg) : "memory");
}

static void __attribute__((noreturn)) stop(void) {
    register uint32_t op __asm__("r0") = 0x18;
    register uint32_t arg __asm__("r1") = 0x20026;

    __asm__ volatile("bkpt 0xab" : "+r"(op) : "r"(arg) : "memory");
    for (;;) {
    }
}

extern unsigned __stack_top[];

int main(void);

void __attribute__((noreturn)) reset(void) {
    main();
    stop();
}

__attribute__((section(".vectors"), used)) static const void *const vectors[2] = {__stack_top, (const void *)reset};
#else
#include "virt.h"
#endif

static void put_text(const char *s) {
    while (*s) put_char(*s++);
}

static void line(const char *name, uint32_t h) {
    put_text(name);
    put_char(' ');
    for (int k = 28; k >= 0; k -= 4) put_char("0123456789abcdef"[(h >> k) & 0xF]);
    put_char('\n');
}

static const uint32_t values[16] = {
    0, 1, 2, 31, 32, 33, 255, 256, 0x7FFFFFFF, 0x80000000, 0x80000001, 0xFFFFFFFE, 0xFFFFFFFF, 0x12345678,
    0xDEADBEEF, 0x0000FF00,
};

// NZCV, every combination but N and Z together; Q clear.
static const uint32_t flags[12] = {
    0x00000000, 0x10000000, 0x20000000, 0x30000000, 0x40000000, 0x50000000, 0x60000000, 0x70000000,
    0x80000000, 0x90000000, 0xA0000000, 0xB0000000,
};

// Every bit of `x` reaches every bit of the hash: the multiply carries bits up, and the shift brings
// the top ones, where the flags are, back down.
static uint32_t mix(uint32_t h, uint32_t x) {
    h = (h ^ x) * 0x5BD1E995u;
    return h ^ (h >> 15);
}

// One instruction, `insn`, with %[d] its destination (holding `b` before it), %[a] and %[b] its
// operands, run on every pair of values from every flag state.
#define CASE(name, insn)                                                                              \
    static void name(void) {                                                                          \
        uint32_t h = 2166136261u;                                                                     \
        for (int i = 0; i < 16; i++)                                                                  \
            for (int j = 0; j < 16; j++)                                                              \
                for (int k = 0; k < 12; k++) {                                                        \
                    uint32_t d = values[j], o;                                                        \
                    __asm__ volatile("msr apsr_nzcvq, %[f]\n\t" insn "\n\tmrs %[o], apsr"             \
                                     : [d] "+&r"(d), [o] "=&r"(o)                                     \
                                     : [a] "r"(values[i]), [b] "r"(values[j]), [f] "r"(flags[k])      \
                                     : "cc");                                                         \
                    h = mix(mix(h, d), o);                                                            \
                }                                                                                     \
        line(#name, h);                                                                               \
    }


// Keep compiler-generated code in the shared scalar ISA, but permit these explicit v8.1-M encodings.
__asm__(".arch armv8.1-m.main");

CASE(csel_eq, "csel %[d], %[a], %[b], eq")
CASE(csel_ne, "csel %[d], %[a], %[b], ne")
CASE(csel_cs, "csel %[d], %[a], %[b], cs")
CASE(csel_cc, "csel %[d], %[a], %[b], cc")
CASE(csel_mi, "csel %[d], %[a], %[b], mi")
CASE(csel_pl, "csel %[d], %[a], %[b], pl")
CASE(csel_vs, "csel %[d], %[a], %[b], vs")
CASE(csel_vc, "csel %[d], %[a], %[b], vc")
CASE(csel_hi, "csel %[d], %[a], %[b], hi")
CASE(csel_ls, "csel %[d], %[a], %[b], ls")
CASE(csel_ge, "csel %[d], %[a], %[b], ge")
CASE(csel_lt, "csel %[d], %[a], %[b], lt")
CASE(csel_gt, "csel %[d], %[a], %[b], gt")
CASE(csel_le, "csel %[d], %[a], %[b], le")
CASE(csinc_eq, "csinc %[d], %[a], %[b], eq")
CASE(csinc_ne, "csinc %[d], %[a], %[b], ne")
CASE(csinc_cs, "csinc %[d], %[a], %[b], cs")
CASE(csinc_cc, "csinc %[d], %[a], %[b], cc")
CASE(csinc_mi, "csinc %[d], %[a], %[b], mi")
CASE(csinc_pl, "csinc %[d], %[a], %[b], pl")
CASE(csinc_vs, "csinc %[d], %[a], %[b], vs")
CASE(csinc_vc, "csinc %[d], %[a], %[b], vc")
CASE(csinc_hi, "csinc %[d], %[a], %[b], hi")
CASE(csinc_ls, "csinc %[d], %[a], %[b], ls")
CASE(csinc_ge, "csinc %[d], %[a], %[b], ge")
CASE(csinc_lt, "csinc %[d], %[a], %[b], lt")
CASE(csinc_gt, "csinc %[d], %[a], %[b], gt")
CASE(csinc_le, "csinc %[d], %[a], %[b], le")
CASE(csinv_eq, "csinv %[d], %[a], %[b], eq")
CASE(csinv_ne, "csinv %[d], %[a], %[b], ne")
CASE(csinv_cs, "csinv %[d], %[a], %[b], cs")
CASE(csinv_cc, "csinv %[d], %[a], %[b], cc")
CASE(csinv_mi, "csinv %[d], %[a], %[b], mi")
CASE(csinv_pl, "csinv %[d], %[a], %[b], pl")
CASE(csinv_vs, "csinv %[d], %[a], %[b], vs")
CASE(csinv_vc, "csinv %[d], %[a], %[b], vc")
CASE(csinv_hi, "csinv %[d], %[a], %[b], hi")
CASE(csinv_ls, "csinv %[d], %[a], %[b], ls")
CASE(csinv_ge, "csinv %[d], %[a], %[b], ge")
CASE(csinv_lt, "csinv %[d], %[a], %[b], lt")
CASE(csinv_gt, "csinv %[d], %[a], %[b], gt")
CASE(csinv_le, "csinv %[d], %[a], %[b], le")
CASE(csneg_eq, "csneg %[d], %[a], %[b], eq")
CASE(csneg_ne, "csneg %[d], %[a], %[b], ne")
CASE(csneg_cs, "csneg %[d], %[a], %[b], cs")
CASE(csneg_cc, "csneg %[d], %[a], %[b], cc")
CASE(csneg_mi, "csneg %[d], %[a], %[b], mi")
CASE(csneg_pl, "csneg %[d], %[a], %[b], pl")
CASE(csneg_vs, "csneg %[d], %[a], %[b], vs")
CASE(csneg_vc, "csneg %[d], %[a], %[b], vc")
CASE(csneg_hi, "csneg %[d], %[a], %[b], hi")
CASE(csneg_ls, "csneg %[d], %[a], %[b], ls")
CASE(csneg_ge, "csneg %[d], %[a], %[b], ge")
CASE(csneg_lt, "csneg %[d], %[a], %[b], lt")
CASE(csneg_gt, "csneg %[d], %[a], %[b], gt")
CASE(csneg_le, "csneg %[d], %[a], %[b], le")
CASE(csel_zr_left, "csel %[d], zr, %[b], eq")
CASE(csel_zr_right, "csel %[d], %[a], zr, eq")
CASE(csel_zr_both, "csel %[d], zr, zr, eq")
CASE(csinc_zr_left, "csinc %[d], zr, %[b], eq")
CASE(csinc_zr_right, "csinc %[d], %[a], zr, eq")
CASE(csinc_zr_both, "csinc %[d], zr, zr, eq")
CASE(csinv_zr_left, "csinv %[d], zr, %[b], eq")
CASE(csinv_zr_right, "csinv %[d], %[a], zr, eq")
CASE(csinv_zr_both, "csinv %[d], zr, zr, eq")
CASE(csneg_zr_left, "csneg %[d], zr, %[b], eq")
CASE(csneg_zr_right, "csneg %[d], %[a], zr, eq")
CASE(csneg_zr_both, "csneg %[d], zr, zr, eq")

int main(void) {
    csel_eq(); csel_ne(); csel_cs(); csel_cc(); csel_mi();
    csel_pl(); csel_vs(); csel_vc(); csel_hi(); csel_ls();
    csel_ge(); csel_lt(); csel_gt(); csel_le(); 
    csinc_eq(); csinc_ne(); csinc_cs(); csinc_cc(); csinc_mi();
    csinc_pl(); csinc_vs(); csinc_vc(); csinc_hi(); csinc_ls();
    csinc_ge(); csinc_lt(); csinc_gt(); csinc_le(); 
    csinv_eq(); csinv_ne(); csinv_cs(); csinv_cc(); csinv_mi();
    csinv_pl(); csinv_vs(); csinv_vc(); csinv_hi(); csinv_ls();
    csinv_ge(); csinv_lt(); csinv_gt(); csinv_le(); 
    csneg_eq(); csneg_ne(); csneg_cs(); csneg_cc(); csneg_mi();
    csneg_pl(); csneg_vs(); csneg_vc(); csneg_hi(); csneg_ls();
    csneg_ge(); csneg_lt(); csneg_gt(); csneg_le(); 
    csel_zr_left(); csel_zr_right(); csel_zr_both(); csinc_zr_left(); csinc_zr_right();
    csinc_zr_both(); csinv_zr_left(); csinv_zr_right(); csinv_zr_both(); csneg_zr_left();
    csneg_zr_right(); csneg_zr_both();
    return 0;
}
