// The floating-point extension as QEMU runs it: each instruction below, run on every pair of 24 edge
// values (zeros, subnormals, the largest and smallest numbers, infinities, quiet and signalling NaNs
// with payloads, and the integers where conversions saturate) under each of the 16 FPSCR settings (the
// four rounding modes, with and without flush-to-zero and default-NaN), its result and FPSCR (NZCV and
// the flags) folded into one hash a line. The same source built for QEMU's Cortex-M4 (`mps2-an386`),
// Cortex-M33 (`mps2-an505`) and double-precision Cortex-M7 (`mps2-an500`), through
// examples/qemu/build-fpu.sh, and for benchtop's boards with the matching unit must print the same
// lines; fpu-m4.qemu, fpu-m33.qemu and fpu-m7.qemu hold QEMU's.
//
// FPv5's instructions are built with -DFPV5, the double-precision ones with -DDOUBLE. The conversions
// to and from half precision also run with and without AHP, the alternative format, so 32 settings.

#include <stdint.h>

#ifdef SEMIHOSTING
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

static const uint32_t values[24] = {
    0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x40200000, 0xC0200000, 0x3EAAAAAB, 0x00800000,
    0x00000001, 0x80400000, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0xFF800000, 0x7FC00000, 0x7FC12345,
    0xFFC00001, 0x7F800001, 0xFFA00000, 0x4B800001, 0x4F000000, 0xCF000000, 0x4F800000, 0x3F7FFFFF,
};

// RMode in bits 23-22, FZ bit 24, DN bit 25, AHP bit 26.
static uint32_t mode(int k) {
    return ((uint32_t)(k & 3) << 22) | ((uint32_t)((k >> 2) & 1) << 24) | ((uint32_t)((k >> 3) & 1) << 25) |
           ((uint32_t)((k >> 4) & 1) << 26);
}

// Half-precision values, two to a word: zeros, subnormals, the smallest normal and the largest, one
// and a third, infinities (65536 under AHP), quiet and signalling NaNs with payloads.
static const uint32_t halves[12] = {
    0x00000001, 0x80003C00, 0x7C00FC00, 0x7E007D01, 0xFE01FD00, 0x7BFF03FF,
    0x04003555, 0xC0008001, 0x3C01BC00, 0x7FFF0400, 0x5640D640, 0x00FF3800,
};

// Singles at the edges of half precision: 65504, 65520 (the tie to infinity), just below it, 2^-24,
// 2^-25 (the tie to zero) and just above it, the smallest normal half, and a subnormal half's worth.
static const uint32_t near_half[8] = {
    0x477FE000, 0x477FF000, 0x477FEFFF, 0x33800000, 0x33000000, 0x33000001, 0x38800000, 0x387FC000,
};

static uint32_t mix(uint32_t h, uint32_t x) {
    h = (h ^ x) * 0x5BD1E995u;
    return h ^ (h >> 15);
}

// A float's bits and back, reinterpreted rather than converted.
union word {
    uint32_t u;
    float f;
};

static float as_float(uint32_t x) {
    union word w = {.u = x};
    return w.f;
}

static uint32_t bits(float f) {
    union word w = {.f = f};
    return w.u;
}

// One instruction, `insn`, with %[d] its destination (holding a third value before it, for the
// accumulating ones), %[a] and %[b] its operands, on every pair of values under every setting.
#define CASE(name, insn)                                                                              \
    static void name(void) {                                                                          \
        uint32_t h = 2166136261u;                                                                     \
        for (int i = 0; i < 24; i++)                                                                  \
            for (int j = 0; j < 24; j++)                                                              \
                for (int k = 0; k < 16; k++) {                                                        \
                    float d = as_float(values[(i + j + k) % 24]);                                     \
                    uint32_t s;                                                                       \
                    __asm__ volatile("vmsr fpscr, %[f]\n\t" insn "\n\tvmrs %[s], fpscr"               \
                                     : [d] "+&t"(d), [s] "=&r"(s)                                     \
                                     : [a] "t"(as_float(values[i])), [b] "t"(as_float(values[j])),    \
                                       [f] "r"(mode(k)));                                             \
                    h = mix(mix(h, bits(d)), s);                                                      \
                }                                                                                     \
        line(#name, h);                                                                               \
    }

CASE(vadd, "vadd.f32 %[d], %[a], %[b]")
CASE(vsub, "vsub.f32 %[d], %[a], %[b]")
CASE(vmul, "vmul.f32 %[d], %[a], %[b]")
CASE(vnmul, "vnmul.f32 %[d], %[a], %[b]")
CASE(vdiv, "vdiv.f32 %[d], %[a], %[b]")
CASE(vmla, "vmla.f32 %[d], %[a], %[b]")
CASE(vmls, "vmls.f32 %[d], %[a], %[b]")
CASE(vnmla, "vnmla.f32 %[d], %[a], %[b]")
CASE(vnmls, "vnmls.f32 %[d], %[a], %[b]")
CASE(vfma, "vfma.f32 %[d], %[a], %[b]")
CASE(vfms, "vfms.f32 %[d], %[a], %[b]")
CASE(vfnma, "vfnma.f32 %[d], %[a], %[b]")
CASE(vfnms, "vfnms.f32 %[d], %[a], %[b]")
CASE(vsqrt, "vsqrt.f32 %[d], %[a]")
CASE(vabs, "vabs.f32 %[d], %[a]")
CASE(vneg, "vneg.f32 %[d], %[a]")
CASE(vmov, "vmov.f32 %[d], %[a]")
CASE(vcmp, "vcmp.f32 %[a], %[b]")
CASE(vcmpe, "vcmpe.f32 %[a], %[b]")
CASE(vcmp_zero, "vcmp.f32 %[a], #0")
CASE(vcmpe_zero, "vcmpe.f32 %[a], #0")
CASE(vcvt_s32, "vcvt.s32.f32 %[d], %[a]")
CASE(vcvt_u32, "vcvt.u32.f32 %[d], %[a]")
CASE(vcvtr_s32, "vcvtr.s32.f32 %[d], %[a]")
CASE(vcvtr_u32, "vcvtr.u32.f32 %[d], %[a]")
CASE(vcvt_from_s32, "vcvt.f32.s32 %[d], %[a]")
CASE(vcvt_from_u32, "vcvt.f32.u32 %[d], %[a]")
CASE(vcvt_s16_fixed, "vmov.f32 %[d], %[a]\n\tvcvt.s16.f32 %[d], %[d], #3")
CASE(vcvt_u32_fixed, "vmov.f32 %[d], %[a]\n\tvcvt.u32.f32 %[d], %[d], #20")
CASE(vcvt_from_s16_fixed, "vmov.f32 %[d], %[a]\n\tvcvt.f32.s16 %[d], %[d], #3")
CASE(vcvt_from_u32_fixed, "vmov.f32 %[d], %[a]\n\tvcvt.f32.u32 %[d], %[d], #20")
CASE(vmov_imm, "vmov.f32 %[d], #-0.1875")

// To single precision from the half in %[a], under all 32 settings.
#define WIDEN(name, insn)                                                                             \
    static void name(void) {                                                                          \
        uint32_t h = 2166136261u;                                                                     \
        for (int i = 0; i < 12; i++)                                                                  \
            for (int k = 0; k < 32; k++) {                                                            \
                float d = as_float(values[(i + k) % 24]);                                             \
                uint32_t s;                                                                           \
                __asm__ volatile("vmsr fpscr, %[f]\n\t" insn "\n\tvmrs %[s], fpscr"                   \
                                 : [d] "+&t"(d), [s] "=&r"(s)                                         \
                                 : [a] "t"(as_float(halves[i])), [f] "r"(mode(k)));                   \
                h = mix(mix(h, bits(d)), s);                                                          \
            }                                                                                         \
        line(#name, h);                                                                               \
    }

// To a half in %[d], whose other half holds another, from the single in %[a].
#define NARROW(name, insn)                                                                            \
    static void name(void) {                                                                          \
        uint32_t h = 2166136261u;                                                                     \
        for (int i = 0; i < 32; i++)                                                                  \
            for (int k = 0; k < 32; k++) {                                                            \
                float d = as_float(halves[(i + k) % 12]);                                             \
                uint32_t s;                                                                           \
                __asm__ volatile("vmsr fpscr, %[f]\n\t" insn "\n\tvmrs %[s], fpscr"                   \
                                 : [d] "+&t"(d), [s] "=&r"(s)                                         \
                                 : [a] "t"(as_float(i < 24 ? values[i] : near_half[i - 24])),         \
                                   [f] "r"(mode(k)));                                                 \
                h = mix(mix(h, bits(d)), s);                                                          \
            }                                                                                         \
        line(#name, h);                                                                               \
    }

WIDEN(vcvtb_from_half, "vcvtb.f32.f16 %[d], %[a]")
WIDEN(vcvtt_from_half, "vcvtt.f32.f16 %[d], %[a]")
NARROW(vcvtb_to_half, "vcvtb.f16.f32 %[d], %[a]")
NARROW(vcvtt_to_half, "vcvtt.f16.f32 %[d], %[a]")

#ifdef FPV5
CASE(vmaxnm, "vmaxnm.f32 %[d], %[a], %[b]")
CASE(vminnm, "vminnm.f32 %[d], %[a], %[b]")
CASE(vrinta, "vrinta.f32 %[d], %[a]")
CASE(vrintn, "vrintn.f32 %[d], %[a]")
CASE(vrintp, "vrintp.f32 %[d], %[a]")
CASE(vrintm, "vrintm.f32 %[d], %[a]")
CASE(vrintz, "vrintz.f32 %[d], %[a]")
CASE(vrintr, "vrintr.f32 %[d], %[a]")
CASE(vrintx, "vrintx.f32 %[d], %[a]")
CASE(vcvta_s32, "vcvta.s32.f32 %[d], %[a]")
CASE(vcvta_u32, "vcvta.u32.f32 %[d], %[a]")
CASE(vcvtn_s32, "vcvtn.s32.f32 %[d], %[a]")
CASE(vcvtn_u32, "vcvtn.u32.f32 %[d], %[a]")
CASE(vcvtp_s32, "vcvtp.s32.f32 %[d], %[a]")
CASE(vcvtm_u32, "vcvtm.u32.f32 %[d], %[a]")

// VSEL under each NZCV but N and Z together, which benchtop's flags do not hold.
#define SEL(name, insn)                                                                               \
    static void name(void) {                                                                          \
        uint32_t h = 2166136261u;                                                                     \
        for (int i = 0; i < 24; i++)                                                                  \
            for (int k = 0; k < 12; k++) {                                                            \
                float d = as_float(0);                                                                \
                __asm__ volatile("msr apsr_nzcvq, %[f]\n\t" insn                                      \
                                 : [d] "+&t"(d)                                                       \
                                 : [a] "t"(as_float(values[i])), [b] "t"(as_float(values[23 - i])),   \
                                   [f] "r"((uint32_t)k << 28)                                         \
                                 : "cc");                                                             \
                h = mix(h, bits(d));                                                                  \
            }                                                                                         \
        line(#name, h);                                                                               \
    }

SEL(vseleq, "vseleq.f32 %[d], %[a], %[b]")
SEL(vselvs, "vselvs.f32 %[d], %[a], %[b]")
SEL(vselge, "vselge.f32 %[d], %[a], %[b]")
SEL(vselgt, "vselgt.f32 %[d], %[a], %[b]")
#endif

#ifdef DOUBLE
static const uint64_t doubles[16] = {
    0x0000000000000000, 0x8000000000000000, 0x3FF0000000000000, 0xBFF0000000000000, 0x3FD5555555555555,
    0x0010000000000000, 0x0000000000000001, 0x7FEFFFFFFFFFFFFF, 0x7FF0000000000000, 0xFFF0000000000000,
    0x7FF8000000000000, 0x7FF8000000012345, 0x7FF0000000000001, 0x41E0000000000000, 0xC1E0000000200000,
    0x3810000000000000,
};

union doubleword {
    uint64_t u;
    double f;
};

static double as_double(uint64_t x) {
    union doubleword w = {.u = x};
    return w.f;
}

static uint64_t dbits(double f) {
    union doubleword w = {.f = f};
    return w.u;
}

#define DCASE(name, insn)                                                                             \
    static void name(void) {                                                                          \
        uint32_t h = 2166136261u;                                                                     \
        for (int i = 0; i < 16; i++)                                                                  \
            for (int j = 0; j < 16; j++)                                                              \
                for (int k = 0; k < 16; k++) {                                                        \
                    double d = as_double(doubles[(i + j + k) % 16]);                                  \
                    uint32_t s;                                                                       \
                    __asm__ volatile("vmsr fpscr, %[f]\n\t" insn "\n\tvmrs %[s], fpscr"               \
                                     : [d] "+&w"(d), [s] "=&r"(s)                                     \
                                     : [a] "w"(as_double(doubles[i])), [b] "w"(as_double(doubles[j])), \
                                       [f] "r"(mode(k)));                                             \
                    uint64_t x = dbits(d);                                                            \
                    h = mix(mix(mix(h, (uint32_t)x), (uint32_t)(x >> 32)), s);                        \
                }                                                                                     \
        line(#name, h);                                                                               \
    }

// The conversions between the precisions and to and from an integer: the destination is D0, whose low
// half is S0, and the operand D1, whose low half is S2.
#define MCASE(name, insn)                                                                             \
    static void name(void) {                                                                          \
        uint32_t h = 2166136261u;                                                                     \
        for (int i = 0; i < 16; i++)                                                                  \
            for (int k = 0; k < 16; k++) {                                                            \
                register double d __asm__("d0") = as_double(0);                                       \
                register double a __asm__("d1") = as_double(doubles[i]);                              \
                uint32_t s;                                                                           \
                __asm__ volatile("vmsr fpscr, %[f]\n\t" insn "\n\tvmrs %[s], fpscr"                   \
                                 : "+w"(d), [s] "=&r"(s)                                              \
                                 : "w"(a), [f] "r"(mode(k)));                                         \
                uint64_t x = dbits(d);                                                                \
                h = mix(mix(mix(h, (uint32_t)x), (uint32_t)(x >> 32)), s);                            \
            }                                                                                         \
        line(#name, h);                                                                               \
    }

DCASE(dadd, "vadd.f64 %P[d], %P[a], %P[b]")
DCASE(dsub, "vsub.f64 %P[d], %P[a], %P[b]")
DCASE(dmul, "vmul.f64 %P[d], %P[a], %P[b]")
DCASE(ddiv, "vdiv.f64 %P[d], %P[a], %P[b]")
DCASE(dmla, "vmla.f64 %P[d], %P[a], %P[b]")
DCASE(dfma, "vfma.f64 %P[d], %P[a], %P[b]")
DCASE(dfnms, "vfnms.f64 %P[d], %P[a], %P[b]")
DCASE(dsqrt, "vsqrt.f64 %P[d], %P[a]")
DCASE(dneg, "vneg.f64 %P[d], %P[a]")
DCASE(dcmpe, "vcmpe.f64 %P[a], %P[b]")
DCASE(dmaxnm, "vmaxnm.f64 %P[d], %P[a], %P[b]")
DCASE(drintx, "vrintx.f64 %P[d], %P[a]")
DCASE(drintn, "vrintn.f64 %P[d], %P[a]")
MCASE(dcvt_s32, "vcvt.s32.f64 s0, d1")
MCASE(dcvtn_s32, "vcvtn.s32.f64 s0, d1")
MCASE(dcvtr_u32, "vcvtr.u32.f64 s0, d1")
MCASE(dcvt_f32, "vcvt.f32.f64 s0, d1")
MCASE(dcvt_f64, "vcvt.f64.f32 d0, s2")
MCASE(dcvt_from_s32, "vcvt.f64.s32 d0, s2")
MCASE(dcvt_fixed, "vmov.f64 d0, d1\n\tvcvt.s32.f64 d0, d0, #16")

// Between double and half precision, under all 32 settings: D0 from the half in S2 (D1's low half),
// and S0, already holding two halves, from D1.
#define DHALF(name, insn, narrowing)                                                                  \
    static void name(void) {                                                                          \
        uint32_t h = 2166136261u;                                                                     \
        for (int i = 0; i < (narrowing ? 16 : 12); i++)                                               \
            for (int k = 0; k < 32; k++) {                                                            \
                register double d __asm__("d0") = as_double(halves[(i + k) % 12]);                    \
                register double a __asm__("d1") = as_double(narrowing ? doubles[i] : halves[i]);      \
                uint32_t s;                                                                           \
                __asm__ volatile("vmsr fpscr, %[f]\n\t" insn "\n\tvmrs %[s], fpscr"                   \
                                 : "+w"(d), [s] "=&r"(s)                                              \
                                 : "w"(a), [f] "r"(mode(k)));                                         \
                uint64_t x = dbits(d);                                                                \
                h = mix(mix(mix(h, (uint32_t)x), (uint32_t)(x >> 32)), s);                            \
            }                                                                                         \
        line(#name, h);                                                                               \
    }

DHALF(dcvtb_from_half, "vcvtb.f64.f16 d0, s2", 0)
DHALF(dcvtt_from_half, "vcvtt.f64.f16 d0, s2", 0)
DHALF(dcvtb_to_half, "vcvtb.f16.f64 s0, d1", 1)
DHALF(dcvtt_to_half, "vcvtt.f16.f64 s0, d1", 1)
#endif

int main(void) {
    // CP10 and CP11, full access.
    *(volatile uint32_t *)0xE000ED88 |= 0xFu << 20;
    __asm__ volatile("dsb\n\tisb");

    vadd();
    vsub();
    vmul();
    vnmul();
    vdiv();
    vmla();
    vmls();
    vnmla();
    vnmls();
    vfma();
    vfms();
    vfnma();
    vfnms();
    vsqrt();
    vabs();
    vneg();
    vmov();
    vcmp();
    vcmpe();
    vcmp_zero();
    vcmpe_zero();
    vcvt_s32();
    vcvt_u32();
    vcvtr_s32();
    vcvtr_u32();
    vcvt_from_s32();
    vcvt_from_u32();
    vcvt_s16_fixed();
    vcvt_u32_fixed();
    vcvt_from_s16_fixed();
    vcvt_from_u32_fixed();
    vmov_imm();
    vcvtb_from_half();
    vcvtt_from_half();
    vcvtb_to_half();
    vcvtt_to_half();
#ifdef FPV5
    vmaxnm();
    vminnm();
    vrinta();
    vrintn();
    vrintp();
    vrintm();
    vrintz();
    vrintr();
    vrintx();
    vcvta_s32();
    vcvta_u32();
    vcvtn_s32();
    vcvtn_u32();
    vcvtp_s32();
    vcvtm_u32();
    vseleq();
    vselvs();
    vselge();
    vselgt();
#endif
#ifdef DOUBLE
    dadd();
    dsub();
    dmul();
    ddiv();
    dmla();
    dfma();
    dfnms();
    dsqrt();
    dneg();
    dcmpe();
    dmaxnm();
    drintx();
    drintn();
    dcvt_s32();
    dcvtn_s32();
    dcvtb_from_half();
    dcvtt_from_half();
    dcvtb_to_half();
    dcvtt_to_half();
    dcvtr_u32();
    dcvt_f32();
    dcvt_f64();
    dcvt_from_s32();
    dcvt_fixed();
#endif
    return 0;
}
