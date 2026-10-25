// Integer DSP differential tests. Regenerate the reference with examples/qemu/build-dsp.sh.
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
    0x7FFF8000, 0xFF007F80,
};

// Every bit of `x` reaches every bit of the hash: the multiply carries bits up, and the shift brings
// the top ones, where the flags are, back down.
static uint32_t mix(uint32_t h, uint32_t x) {
    h = (h ^ x) * 0x5BD1E995u;
    return h ^ (h >> 15);
}


// Fixed registers make the same encodings useful for the instruction-level unit tests. The
// accumulator, NZCVQ and all GE combinations vary independently of the two multiplicands.
#define CASE(name, insn) \
static void name(void) { \
    uint32_t h = 2166136261u; \
    for (int i=0; i<16; i++) for (int j=0; j<16; j++) for (int k=0; k<16; k++) { \
        register uint32_t d __asm__("r0") = values[k]; \
        register uint32_t a __asm__("r1") = values[i]; \
        register uint32_t b __asm__("r2") = values[j]; \
        register uint32_t c __asm__("r3") = values[15-k]; \
        uint32_t f = ((k % 12) << 28) | ((k & 1) << 27) | (k << 16), o; \
        __asm__ volatile("msr apsr_nzcvqg, %[f]\n\t" insn "\n\tmrs %[o], apsr" \
            : "+r"(d), "+r"(c), [o] "=&r"(o) : "r"(a), "r"(b), [f] "r"(f) : "cc"); \
        h = mix(mix(mix(h, d), c), o); \
    } \
    line(#name,h); \
}
CASE(sadd8, "sadd8 r0,r1,r2")
CASE(sadd16, "sadd16 r0,r1,r2")
CASE(sasx, "sasx r0,r1,r2")
CASE(ssax, "ssax r0,r1,r2")
CASE(ssub8, "ssub8 r0,r1,r2")
CASE(ssub16, "ssub16 r0,r1,r2")
CASE(qadd8, "qadd8 r0,r1,r2")
CASE(qadd16, "qadd16 r0,r1,r2")
CASE(qasx, "qasx r0,r1,r2")
CASE(qsax, "qsax r0,r1,r2")
CASE(qsub8, "qsub8 r0,r1,r2")
CASE(qsub16, "qsub16 r0,r1,r2")
CASE(shadd8, "shadd8 r0,r1,r2")
CASE(shadd16, "shadd16 r0,r1,r2")
CASE(shasx, "shasx r0,r1,r2")
CASE(shsax, "shsax r0,r1,r2")
CASE(shsub8, "shsub8 r0,r1,r2")
CASE(shsub16, "shsub16 r0,r1,r2")
CASE(uadd8, "uadd8 r0,r1,r2")
CASE(uadd16, "uadd16 r0,r1,r2")
CASE(uasx, "uasx r0,r1,r2")
CASE(usax, "usax r0,r1,r2")
CASE(usub8, "usub8 r0,r1,r2")
CASE(usub16, "usub16 r0,r1,r2")
CASE(uqadd8, "uqadd8 r0,r1,r2")
CASE(uqadd16, "uqadd16 r0,r1,r2")
CASE(uqasx, "uqasx r0,r1,r2")
CASE(uqsax, "uqsax r0,r1,r2")
CASE(uqsub8, "uqsub8 r0,r1,r2")
CASE(uqsub16, "uqsub16 r0,r1,r2")
CASE(uhadd8, "uhadd8 r0,r1,r2")
CASE(uhadd16, "uhadd16 r0,r1,r2")
CASE(uhasx, "uhasx r0,r1,r2")
CASE(uhsax, "uhsax r0,r1,r2")
CASE(uhsub8, "uhsub8 r0,r1,r2")
CASE(uhsub16, "uhsub16 r0,r1,r2")
CASE(qadd, "qadd r0,r1,r2")
CASE(qdadd, "qdadd r0,r1,r2")
CASE(qsub, "qsub r0,r1,r2")
CASE(qdsub, "qdsub r0,r1,r2")
CASE(sel, "sel r0,r1,r2")
CASE(pkhbt_0, "pkhbt r0,r1,r2,lsl #0")
CASE(pkhbt_1, "pkhbt r0,r1,r2,lsl #1")
CASE(pkhbt_16, "pkhbt r0,r1,r2,lsl #16")
CASE(pkhbt_31, "pkhbt r0,r1,r2,lsl #31")
CASE(pkhtb_1, "pkhtb r0,r1,r2,asr #1")
CASE(pkhtb_16, "pkhtb r0,r1,r2,asr #16")
CASE(pkhtb_31, "pkhtb r0,r1,r2,asr #31")
CASE(pkhtb_32, "pkhtb r0,r1,r2,asr #32")
CASE(ssat16_1, "ssat16 r0,#1,r1")
CASE(ssat16_2, "ssat16 r0,#2,r1")
CASE(ssat16_3, "ssat16 r0,#3,r1")
CASE(ssat16_4, "ssat16 r0,#4,r1")
CASE(ssat16_5, "ssat16 r0,#5,r1")
CASE(ssat16_6, "ssat16 r0,#6,r1")
CASE(ssat16_7, "ssat16 r0,#7,r1")
CASE(ssat16_8, "ssat16 r0,#8,r1")
CASE(ssat16_9, "ssat16 r0,#9,r1")
CASE(ssat16_10, "ssat16 r0,#10,r1")
CASE(ssat16_11, "ssat16 r0,#11,r1")
CASE(ssat16_12, "ssat16 r0,#12,r1")
CASE(ssat16_13, "ssat16 r0,#13,r1")
CASE(ssat16_14, "ssat16 r0,#14,r1")
CASE(ssat16_15, "ssat16 r0,#15,r1")
CASE(ssat16_16, "ssat16 r0,#16,r1")
CASE(usat16_0, "usat16 r0,#0,r1")
CASE(usat16_1, "usat16 r0,#1,r1")
CASE(usat16_2, "usat16 r0,#2,r1")
CASE(usat16_3, "usat16 r0,#3,r1")
CASE(usat16_4, "usat16 r0,#4,r1")
CASE(usat16_5, "usat16 r0,#5,r1")
CASE(usat16_6, "usat16 r0,#6,r1")
CASE(usat16_7, "usat16 r0,#7,r1")
CASE(usat16_8, "usat16 r0,#8,r1")
CASE(usat16_9, "usat16 r0,#9,r1")
CASE(usat16_10, "usat16 r0,#10,r1")
CASE(usat16_11, "usat16 r0,#11,r1")
CASE(usat16_12, "usat16 r0,#12,r1")
CASE(usat16_13, "usat16 r0,#13,r1")
CASE(usat16_14, "usat16 r0,#14,r1")
CASE(usat16_15, "usat16 r0,#15,r1")
CASE(sxtab_0, "sxtab r0,r1,r2,ror #0")
CASE(sxtab_8, "sxtab r0,r1,r2,ror #8")
CASE(sxtab_16, "sxtab r0,r1,r2,ror #16")
CASE(sxtab_24, "sxtab r0,r1,r2,ror #24")
CASE(sxtah_0, "sxtah r0,r1,r2,ror #0")
CASE(sxtah_8, "sxtah r0,r1,r2,ror #8")
CASE(sxtah_16, "sxtah r0,r1,r2,ror #16")
CASE(sxtah_24, "sxtah r0,r1,r2,ror #24")
CASE(sxtab16_0, "sxtab16 r0,r1,r2,ror #0")
CASE(sxtab16_8, "sxtab16 r0,r1,r2,ror #8")
CASE(sxtab16_16, "sxtab16 r0,r1,r2,ror #16")
CASE(sxtab16_24, "sxtab16 r0,r1,r2,ror #24")
CASE(uxtab_0, "uxtab r0,r1,r2,ror #0")
CASE(uxtab_8, "uxtab r0,r1,r2,ror #8")
CASE(uxtab_16, "uxtab r0,r1,r2,ror #16")
CASE(uxtab_24, "uxtab r0,r1,r2,ror #24")
CASE(uxtah_0, "uxtah r0,r1,r2,ror #0")
CASE(uxtah_8, "uxtah r0,r1,r2,ror #8")
CASE(uxtah_16, "uxtah r0,r1,r2,ror #16")
CASE(uxtah_24, "uxtah r0,r1,r2,ror #24")
CASE(uxtab16_0, "uxtab16 r0,r1,r2,ror #0")
CASE(uxtab16_8, "uxtab16 r0,r1,r2,ror #8")
CASE(uxtab16_16, "uxtab16 r0,r1,r2,ror #16")
CASE(uxtab16_24, "uxtab16 r0,r1,r2,ror #24")
CASE(sxtb16_0, "sxtb16 r0,r2,ror #0")
CASE(sxtb16_8, "sxtb16 r0,r2,ror #8")
CASE(sxtb16_16, "sxtb16 r0,r2,ror #16")
CASE(sxtb16_24, "sxtb16 r0,r2,ror #24")
CASE(uxtb16_0, "uxtb16 r0,r2,ror #0")
CASE(uxtb16_8, "uxtb16 r0,r2,ror #8")
CASE(uxtb16_16, "uxtb16 r0,r2,ror #16")
CASE(uxtb16_24, "uxtb16 r0,r2,ror #24")
CASE(smlabb, "smlabb r0,r1,r2,r3")
CASE(smlabt, "smlabt r0,r1,r2,r3")
CASE(smlatb, "smlatb r0,r1,r2,r3")
CASE(smlatt, "smlatt r0,r1,r2,r3")
CASE(smulbb, "smulbb r0,r1,r2")
CASE(smulbt, "smulbt r0,r1,r2")
CASE(smultb, "smultb r0,r1,r2")
CASE(smultt, "smultt r0,r1,r2")
CASE(smlalbb, "smlalbb r0,r3,r1,r2")
CASE(smlalbt, "smlalbt r0,r3,r1,r2")
CASE(smlaltb, "smlaltb r0,r3,r1,r2")
CASE(smlaltt, "smlaltt r0,r3,r1,r2")
CASE(smlawb, "smlawb r0,r1,r2,r3")
CASE(smlawt, "smlawt r0,r1,r2,r3")
CASE(smulwb, "smulwb r0,r1,r2")
CASE(smulwt, "smulwt r0,r1,r2")
CASE(smlad, "smlad r0,r1,r2,r3")
CASE(smladx, "smladx r0,r1,r2,r3")
CASE(smlsd, "smlsd r0,r1,r2,r3")
CASE(smlsdx, "smlsdx r0,r1,r2,r3")
CASE(smuad, "smuad r0,r1,r2")
CASE(smuadx, "smuadx r0,r1,r2")
CASE(smusd, "smusd r0,r1,r2")
CASE(smusdx, "smusdx r0,r1,r2")
CASE(smmla, "smmla r0,r1,r2,r3")
CASE(smmlar, "smmlar r0,r1,r2,r3")
CASE(smmls, "smmls r0,r1,r2,r3")
CASE(smmlsr, "smmlsr r0,r1,r2,r3")
CASE(smmul, "smmul r0,r1,r2")
CASE(smmulr, "smmulr r0,r1,r2")
CASE(usad8, "usad8 r0,r1,r2")
CASE(usada8, "usada8 r0,r1,r2,r3")
CASE(smlald, "smlald r0,r3,r1,r2")
CASE(smlaldx, "smlaldx r0,r3,r1,r2")
CASE(smlsld, "smlsld r0,r3,r1,r2")
CASE(smlsldx, "smlsldx r0,r3,r1,r2")
CASE(umaal, "umaal r0,r3,r1,r2")
CASE(smlad_overlap, "smlad r0,r0,r2,r3")
CASE(smlald_overlap, "smlald r0,r3,r0,r3")

int main(void) {
    sadd8();
    sadd16();
    sasx();
    ssax();
    ssub8();
    ssub16();
    qadd8();
    qadd16();
    qasx();
    qsax();
    qsub8();
    qsub16();
    shadd8();
    shadd16();
    shasx();
    shsax();
    shsub8();
    shsub16();
    uadd8();
    uadd16();
    uasx();
    usax();
    usub8();
    usub16();
    uqadd8();
    uqadd16();
    uqasx();
    uqsax();
    uqsub8();
    uqsub16();
    uhadd8();
    uhadd16();
    uhasx();
    uhsax();
    uhsub8();
    uhsub16();
    qadd();
    qdadd();
    qsub();
    qdsub();
    sel();
    pkhbt_0();
    pkhbt_1();
    pkhbt_16();
    pkhbt_31();
    pkhtb_1();
    pkhtb_16();
    pkhtb_31();
    pkhtb_32();
    ssat16_1();
    ssat16_2();
    ssat16_3();
    ssat16_4();
    ssat16_5();
    ssat16_6();
    ssat16_7();
    ssat16_8();
    ssat16_9();
    ssat16_10();
    ssat16_11();
    ssat16_12();
    ssat16_13();
    ssat16_14();
    ssat16_15();
    ssat16_16();
    usat16_0();
    usat16_1();
    usat16_2();
    usat16_3();
    usat16_4();
    usat16_5();
    usat16_6();
    usat16_7();
    usat16_8();
    usat16_9();
    usat16_10();
    usat16_11();
    usat16_12();
    usat16_13();
    usat16_14();
    usat16_15();
    sxtab_0();
    sxtab_8();
    sxtab_16();
    sxtab_24();
    sxtah_0();
    sxtah_8();
    sxtah_16();
    sxtah_24();
    sxtab16_0();
    sxtab16_8();
    sxtab16_16();
    sxtab16_24();
    uxtab_0();
    uxtab_8();
    uxtab_16();
    uxtab_24();
    uxtah_0();
    uxtah_8();
    uxtah_16();
    uxtah_24();
    uxtab16_0();
    uxtab16_8();
    uxtab16_16();
    uxtab16_24();
    sxtb16_0();
    sxtb16_8();
    sxtb16_16();
    sxtb16_24();
    uxtb16_0();
    uxtb16_8();
    uxtb16_16();
    uxtb16_24();
    smlabb();
    smlabt();
    smlatb();
    smlatt();
    smulbb();
    smulbt();
    smultb();
    smultt();
    smlalbb();
    smlalbt();
    smlaltb();
    smlaltt();
    smlawb();
    smlawt();
    smulwb();
    smulwt();
    smlad();
    smladx();
    smlsd();
    smlsdx();
    smuad();
    smuadx();
    smusd();
    smusdx();
    smmla();
    smmlar();
    smmls();
    smmlsr();
    smmul();
    smmulr();
    usad8();
    usada8();
    smlald();
    smlaldx();
    smlsld();
    smlsldx();
    umaal();
    smlad_overlap();
    smlald_overlap();
    return 0;
}
