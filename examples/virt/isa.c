// The integer instruction set as QEMU's Cortex-M33 runs it: each instruction below, run on every pair
// of 16 edge values and from each of the 12 flag states, its result and APSR (N, Z, C, V and Q) folded
// into one hash a line. The same source built for QEMU's `mps2-an505` (`-DSEMIHOSTING`, through
// examples/qemu/build.sh) and for `virt33` must print the same lines, so the M33 benchtop emulates is
// checked against an emulator nobody here wrote. isa.qemu holds QEMU's lines.
//
// The flag states leave out N and Z both set, which no instruction produces and benchtop's flags do
// not hold (DESIGN §2.9).

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

// The same with low registers, for the 16-bit encodings an IT block makes flagless.
#define LOW(name, insn)                                                                               \
    static void name(void) {                                                                          \
        uint32_t h = 2166136261u;                                                                     \
        for (int i = 0; i < 16; i++)                                                                  \
            for (int j = 0; j < 16; j++)                                                              \
                for (int k = 0; k < 12; k++) {                                                        \
                    uint32_t d = values[j], o;                                                        \
                    __asm__ volatile("msr apsr_nzcvq, %[f]\n\t" insn "\n\tmrs %[o], apsr"             \
                                     : [d] "+&l"(d), [o] "=&r"(o)                                     \
                                     : [a] "l"(values[i]), [b] "l"(values[j]), [f] "r"(flags[k])      \
                                     : "cc");                                                         \
                    h = mix(mix(h, d), o);                                                            \
                }                                                                                     \
        line(#name, h);                                                                               \
    }

// A long multiply: %[d] and %[e] its two halves, holding `a` and `b` before it.
#define LONG(name, insn)                                                                              \
    static void name(void) {                                                                          \
        uint32_t h = 2166136261u;                                                                     \
        for (int i = 0; i < 16; i++)                                                                  \
            for (int j = 0; j < 16; j++) {                                                            \
                uint32_t d = values[i], e = values[j];                                                \
                __asm__ volatile(insn : [d] "+&r"(d), [e] "+&r"(e) : [a] "r"(values[i]), [b] "r"(values[j])); \
                h = mix(mix(h, d), e);                                                                \
            }                                                                                         \
        line(#name, h);                                                                               \
    }

CASE(adds, "adds %[d], %[a], %[b]")
CASE(adcs, "adcs %[d], %[a], %[b]")
CASE(subs, "subs %[d], %[a], %[b]")
CASE(sbcs, "sbcs %[d], %[a], %[b]")
CASE(rsbs, "rsbs %[d], %[a], %[b]")
CASE(adds_lsl31, "adds %[d], %[a], %[b], lsl #31")
CASE(subs_asr1, "subs %[d], %[a], %[b], asr #1")
CASE(adcs_rrx, "adcs %[d], %[a], %[b], rrx")
CASE(sbcs_lsr32, "sbcs %[d], %[a], %[b], lsr #32")
CASE(rsb_lsl4, "rsb %[d], %[a], %[b], lsl #4")
CASE(add_ror9, "add %[d], %[a], %[b], ror #9")
CASE(ands_lsl1, "ands %[d], %[a], %[b], lsl #1")
CASE(orrs_lsr1, "orrs %[d], %[a], %[b], lsr #1")
CASE(eors_asr31, "eors %[d], %[a], %[b], asr #31")
CASE(eors_asr32, "eors %[d], %[a], %[b], asr #32")
CASE(bics_ror7, "bics %[d], %[a], %[b], ror #7")
CASE(orns_rrx, "orns %[d], %[a], %[b], rrx")
CASE(mvns_lsr32, "mvns %[d], %[b], lsr #32")
CASE(movs_lsl31, "lsls %[d], %[a], #31")
CASE(movs_lsr32, "lsrs %[d], %[a], #32")
CASE(movs_asr32, "asrs %[d], %[a], #32")
CASE(movs_ror1, "rors %[d], %[a], #1")
CASE(movs_rrx, "rrxs %[d], %[a]")
CASE(cmp, "cmp %[a], %[b]")
CASE(cmn, "cmn %[a], %[b]")
CASE(cmp_lsl3, "cmp.w %[a], %[b], lsl #3")
CASE(tst_lsl1, "tst.w %[a], %[b], lsl #1")
CASE(teq_lsr1, "teq %[a], %[b], lsr #1")
CASE(lsls_reg, "lsls.w %[d], %[a], %[b]")
CASE(lsrs_reg, "lsrs.w %[d], %[a], %[b]")
CASE(asrs_reg, "asrs.w %[d], %[a], %[b]")
CASE(rors_reg, "rors.w %[d], %[a], %[b]")
CASE(lsl_reg, "lsl.w %[d], %[a], %[b]")
CASE(lsr_reg, "lsr.w %[d], %[a], %[b]")
CASE(asr_reg, "asr.w %[d], %[a], %[b]")
CASE(ror_reg, "ror.w %[d], %[a], %[b]")
CASE(adds_imm, "adds.w %[d], %[a], #0x80000000")
CASE(subs_imm, "subs.w %[d], %[a], #1")
CASE(adcs_imm, "adcs %[d], %[a], #0x10000")
CASE(sbcs_imm, "sbcs %[d], %[a], #0x10000")
CASE(rsbs_imm, "rsbs.w %[d], %[a], #0")
CASE(cmp_imm, "cmp.w %[a], #0x80000000")
CASE(cmn_imm, "cmn.w %[a], #1")
CASE(ands_rot, "ands %[d], %[a], #0x3FC")
CASE(ands_pattern, "ands %[d], %[a], #0xFF00FF00")
CASE(orrs_top, "orrs %[d], %[a], #0x80000000")
CASE(eors_plain, "eors %[d], %[a], #0xAB")
CASE(bics_rot, "bics %[d], %[a], #0x40000000")
CASE(orns_rot, "orns %[d], %[a], #0x0000FF00")
CASE(mvns_rot, "mvns %[d], #0x3FC")
CASE(movs_rot, "movs.w %[d], #0x80000000")
CASE(tst_rot, "tst.w %[a], #0x80000000")
CASE(teq_plain, "teq.w %[a], #0xFF")
CASE(addw, "addw %[d], %[a], #4095")
CASE(subw, "subw %[d], %[a], #4095")
CASE(movw, "movw %[d], #0xBEEF")
CASE(movt, "movt %[d], #0xBEEF")
CASE(bfi, "bfi %[d], %[a], #3, #9")
CASE(bfc, "bfc %[d], #5, #20")
CASE(ubfx, "ubfx %[d], %[a], #7, #13")
CASE(sbfx, "sbfx %[d], %[a], #7, #13")
CASE(sbfx_all, "sbfx %[d], %[a], #0, #32")
CASE(clz, "clz %[d], %[a]")
CASE(rbit, "rbit %[d], %[a]")
CASE(rev, "rev.w %[d], %[a]")
CASE(rev16, "rev16.w %[d], %[a]")
CASE(revsh, "revsh.w %[d], %[a]")
CASE(sxtb_ror8, "sxtb.w %[d], %[a], ror #8")
CASE(sxth_ror16, "sxth.w %[d], %[a], ror #16")
CASE(uxtb_ror24, "uxtb.w %[d], %[a], ror #24")
CASE(uxth, "uxth.w %[d], %[a]")
CASE(mul, "mul.w %[d], %[a], %[b]")
CASE(mla, "mla %[d], %[a], %[b], %[d]")
CASE(mls, "mls %[d], %[a], %[b], %[d]")
CASE(udiv, "udiv %[d], %[a], %[b]")
CASE(sdiv, "sdiv %[d], %[a], %[b]")
CASE(ssat8, "ssat %[d], #8, %[a]")
CASE(ssat1_lsl3, "ssat %[d], #1, %[a], lsl #3")
CASE(ssat32_asr5, "ssat %[d], #32, %[a], asr #5")
CASE(usat8, "usat %[d], #8, %[a]")
CASE(usat0, "usat %[d], #0, %[a]")
CASE(usat31_asr1, "usat %[d], #31, %[a], asr #1")
CASE(it_eq, "ite eq\n\taddeq.w %[d], %[a], %[b]\n\tsubne.w %[d], %[a], %[b]")
CASE(it_hi, "itet hi\n\taddhi.w %[d], %[a], #1\n\tsubls.w %[d], %[a], #1\n\teorhi.w %[d], %[d], %[b]")
CASE(it_ge, "itte ge\n\tmovge %[d], %[a]\n\taddsge.w %[d], %[d], %[b]\n\tmvnlt %[d], %[b]")
CASE(it_lt_flags, "itete lt\n\tcmplt %[a], %[b]\n\taddge.w %[d], %[d], #5\n\tsublt.w %[d], %[d], #7\n\tmovge %[d], %[a]")
CASE(it_vs, "itt vs\n\tmovvs %[d], #1\n\tmovvs %[d], #2")
CASE(it_cs_cc, "ite cs\n\tlslcs.w %[d], %[a], #4\n\tlsrcc.w %[d], %[a], #4")
LOW(it_low_eq, "itttt eq\n\taddeq %[d], %[a]\n\tsubeq %[d], %[b]\n\tlsleq %[d], %[d], #3\n\tmvneq %[d], %[d]")
LOW(it_low_ne, "itete ne\n\tlslne %[d], %[a]\n\tasreq %[d], %[b]\n\tandne %[d], %[a]\n\torreq %[d], %[b]")
LOW(it_low_mi, "itee mi\n\tadcmi %[d], %[a]\n\tsbcpl %[d], %[b]\n\tnegpl %[d], %[a]")
LOW(it_low_gt, "ittt gt\n\trorgt %[d], %[a]\n\tbicgt %[d], %[b]\n\tmulgt %[d], %[a], %[d]")
LONG(umull, "umull %[d], %[e], %[a], %[b]")
LONG(smull, "smull %[d], %[e], %[a], %[b]")
LONG(umlal, "umlal %[d], %[e], %[a], %[b]")
LONG(smlal, "smlal %[d], %[e], %[a], %[b]")

int main(void) {
    adds(); adcs(); subs(); sbcs(); rsbs(); adds_lsl31(); subs_asr1(); adcs_rrx(); sbcs_lsr32();
    rsb_lsl4(); add_ror9(); ands_lsl1(); orrs_lsr1(); eors_asr31(); eors_asr32(); bics_ror7();
    orns_rrx(); mvns_lsr32(); movs_lsl31(); movs_lsr32(); movs_asr32(); movs_ror1(); movs_rrx();
    cmp(); cmn(); cmp_lsl3(); tst_lsl1(); teq_lsr1();
    lsls_reg(); lsrs_reg(); asrs_reg(); rors_reg(); lsl_reg(); lsr_reg(); asr_reg(); ror_reg();
    adds_imm(); subs_imm(); adcs_imm(); sbcs_imm(); rsbs_imm(); cmp_imm(); cmn_imm();
    ands_rot(); ands_pattern(); orrs_top(); eors_plain(); bics_rot(); orns_rot(); mvns_rot();
    movs_rot(); tst_rot(); teq_plain(); addw(); subw(); movw(); movt();
    bfi(); bfc(); ubfx(); sbfx(); sbfx_all(); clz(); rbit(); rev(); rev16(); revsh();
    sxtb_ror8(); sxth_ror16(); uxtb_ror24(); uxth(); mul(); mla(); mls(); udiv(); sdiv();
    ssat8(); ssat1_lsl3(); ssat32_asr5(); usat8(); usat0(); usat31_asr1();
    it_eq(); it_hi(); it_ge(); it_lt_flags(); it_vs(); it_cs_cc();
    it_low_eq(); it_low_ne(); it_low_mi(); it_low_gt();
    umull(); smull(); umlal(); smlal();
    return 0;
}
