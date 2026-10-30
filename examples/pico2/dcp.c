// The double-precision coprocessor, through pico_double (C's double arithmetic, sqrt, fma, the
// comparisons and casts, ddiv_fast and sqrt_fast) and through the SDK's canned sequences directly
// (dcp_ops.S: single precision, the raw comparison and classify words, every conversion).
//
// Every operation runs on the same pseudo-random operands — zeros, denormals, infinities, NaNs, the
// edges of the range and of the integer words, and pairs that cancel — and folds its results into
// one hash per operation, which chip/dcp_tests.sysl recomputes with the host's IEEE arithmetic. Core 1
// runs additions, multiplications and divisions of its own at the same time, and a repeating timer
// interrupts core 0 with double arithmetic of its own, so some interrupts land inside a sequence and
// the SDK saves and restores the coprocessor around them. Then printf("%f") and friends.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/double.h"

uint32_t t_fadd(uint32_t, uint32_t), t_fsub(uint32_t, uint32_t), t_fmul(uint32_t, uint32_t);
uint32_t t_fdiv(uint32_t, uint32_t), t_fdiv_fast(uint32_t, uint32_t), t_fsqrt(uint32_t), t_fsqrt_fast(uint32_t);
uint32_t t_fcmp(uint32_t, uint32_t), t_dcmp(uint64_t, uint64_t), t_dclassify(uint64_t);
uint64_t t_fxprod(uint32_t, uint32_t), t_ddiv_fast(uint64_t, uint64_t), t_dsqrt_fast(uint64_t);
uint64_t t_f2d(uint32_t), t_i2d(uint32_t), t_ui2d(uint32_t);
uint32_t t_d2f(uint64_t), t_i2f(uint32_t), t_ui2f(uint32_t);
uint32_t t_d2i(uint64_t), t_d2ui(uint64_t), t_d2i_r(uint64_t), t_d2ui_r(uint64_t);
uint32_t t_f2i(uint32_t), t_f2ui(uint32_t), t_f2i_r(uint32_t), t_f2ui_r(uint32_t);
uint32_t t_pcmp(void);

static double d(uint64_t x) { double r; memcpy(&r, &x, 8); return r; }
static uint64_t b(double x) { uint64_t r; memcpy(&r, &x, 8); return r; }

static const uint64_t EDGES[32] = {
    0x0000000000000000, 0x8000000000000000, 0x0010000000000000, 0x8010000000000000,
    0x000FFFFFFFFFFFFF, 0x0000000000000001, 0x7FEFFFFFFFFFFFFF, 0xFFEFFFFFFFFFFFFF, 0x7FF0000000000000, 0xFFF0000000000000,
    0x7FF8000000000000, 0x7FF0000000000001, 0xFFF8000000000123, 0x3FF0000000000000, 0xBFF0000000000000, 0x3FEFFFFFFFFFFFFF,
    0x3FF0000000000001, 0x4000000000000000, 0x3FE0000000000000, 0xBFE0000000000000, 0x3FF8000000000000, 0xC004000000000000,
    0x41E0000000000000, 0xC1E0000000000000, 0x41DFFFFFFFC00000, 0x41EFFFFFFFE00000, 0x41F0000000000000, 0x4330000000000000,
    0xC330000000000000, 0x0020000000000000, 0x3CB0000000000000, 0x4340000000000000};

// The operand generator, step for step as chip/dcp_tests.sysl's `Operands`.
typedef struct { uint64_t s; } gen_t;

static uint64_t next(gen_t *g) {
    g->s ^= g->s << 13;
    g->s ^= g->s >> 7;
    g->s ^= g->s << 17;
    return g->s;
}

static uint64_t dbl(gen_t *g) {
    uint64_t r = next(g);
    uint64_t pick = next(g) % 8;
    if (pick == 0) return EDGES[r % 32];
    uint64_t e = pick == 1 ? (r >> 53) & 0x7FF : pick == 2 ? 1 + (r >> 53) % 60 : pick == 3 ? 2046 - (r >> 53) % 60 : 1023 - 40 + (r >> 53) % 80;
    uint64_t frac = pick == 4 ? r & 0xFFFFF00000000 : pick == 5 ? (r & 0xFFFFFFFFFFFFF) | 0xFFFFFFFFFFF00 : r & 0xFFFFFFFFFFFFF;
    return (r & 0x8000000000000000) | (e << 52) | frac;
}

static uint32_t sgl(gen_t *g) {
    uint64_t x = dbl(g);
    uint64_t r = next(g);
    uint64_t pick = r % 4;
    if (pick == 0) return (uint32_t)(x >> 32);
    uint64_t e = pick == 1 ? 1 + (r >> 50) % 30 : pick == 2 ? 254 - (r >> 50) % 30 : 127 - 20 + (r >> 50) % 40;
    return (uint32_t)(((x >> 32) & 0x80000000) | (e << 23) | (x & 0x7FFFFF));
}

// A second operand: half the time a fresh one, half the time one near the first.
static uint64_t dbl_near(gen_t *g, uint64_t x) {
    if ((next(g) & 1) == 0) return dbl(g);
    uint64_t a = next(g);
    uint64_t c = next(g);
    return x ^ (a & 0x800FFFFFFFFFFFFF) ^ (c & 0x0030000000000000);
}

static uint32_t sgl_near(gen_t *g, uint32_t x) {
    if ((next(g) & 1) == 0) return sgl(g);
    uint64_t a = next(g);
    uint64_t c = next(g);
    return x ^ (uint32_t)(a & 0x807FFFFF) ^ (uint32_t)(c & 0x01800000);
}

// FNV-1a over 64-bit words.
static uint64_t mix(uint64_t h, uint64_t x) { return (h ^ x) * 0x100000001B3ull; }
#define FNV 0xCBF29CE484222325ull

enum { DADD, DSUB, DMUL, DDIV, DSQRT, DSQRT_FAST, DDIV_FAST, DFMA, DCMP_C, DCMP_RAW, DCLASS, D2IZ, D2UIZ, I2D, UI2D, D2F,
    D2I_R, D2UI_R, FADD, FSUB, FMUL, FDIV, FDIV_FAST, FSQRT, FSQRT_FAST, FCMP, FXPROD, F2D, I2F, UI2F, F2I, F2UI, F2I_R,
    F2UI_R, OPS };
static const char *const NAMES[OPS] = {"dadd", "dsub", "dmul", "ddiv", "dsqrt", "dsqrt_fast", "ddiv_fast", "dfma",
    "dcmp_c", "dcmp_raw", "dclassify", "d2iz", "d2uiz", "i2d", "ui2d", "d2f", "d2i_r", "d2ui_r", "fadd", "fsub", "fmul",
    "fdiv", "fdiv_fast", "fsqrt", "fsqrt_fast", "fcmp", "fxprod", "f2d", "i2f", "ui2f", "f2i", "f2ui", "f2i_r", "f2ui_r"};

#define ROUNDS 3000

// C's six comparisons of x and y, one bit each, through __aeabi_dcmp*.
static uint64_t compare(double x, double y) {
    return (x < y) | (x <= y) << 1 | (x == y) << 2 | (x != y) << 3 | (x >= y) << 4 | (x > y) << 5 | isunordered(x, y) << 6;
}

// Core 1: additions, multiplications and divisions of its own operands, its hash sent through the FIFO.
static void core1_main(void) {
    gen_t g = {0x0123456789ABCDEF};
    uint64_t h = FNV;
    for (int i = 0; i < ROUNDS; i++) {
        uint64_t x = dbl(&g);
        uint64_t y = dbl_near(&g, x);
        h = mix(h, b(d(x) + d(y)));
        h = mix(h, b(d(x) * d(y)));
        h = mix(h, b(d(x) / d(y)));
    }
    multicore_fifo_push_blocking((uint32_t)h);
    multicore_fifo_push_blocking((uint32_t)(h >> 32));
}

// The interrupt: double arithmetic of its own, checked exactly, counting the times it found core 0
// inside a sequence (the engaged flag set).
static volatile uint32_t irqs, inside, wrong;

static bool tick(repeating_timer_t *t) {
    if (t_pcmp() >> 31) inside++;
    uint32_t k = irqs + 1;
    double r = sqrt((double)k * (double)k) * 3.0 / 3.0 + 0.5;
    if (r != (double)k + 0.5) wrong++;
    irqs = k;
    return true;
}

int main(void) {
    stdio_init_all();
    multicore_launch_core1(core1_main);
    repeating_timer_t timer;
    add_repeating_timer_us(-20, tick, NULL, &timer);

    uint64_t h[OPS];
    for (int k = 0; k < OPS; k++) h[k] = FNV;
    gen_t g = {0x9E3779B97F4A7C15};
    for (int i = 0; i < ROUNDS; i++) {
        uint64_t x = dbl(&g);
        uint64_t y = dbl_near(&g, x);
        uint64_t z = dbl(&g);
        uint32_t fx = sgl(&g);
        uint32_t fy = sgl_near(&g, fx);
        uint32_t n = (uint32_t)next(&g);
        double dx = d(x), dy = d(y);
        h[DADD] = mix(h[DADD], b(dx + dy));
        h[DSUB] = mix(h[DSUB], b(dx - dy));
        h[DMUL] = mix(h[DMUL], b(dx * dy));
        h[DDIV] = mix(h[DDIV], b(dx / dy));
        h[DSQRT] = mix(h[DSQRT], b(sqrt(dx)));
        h[DSQRT_FAST] = mix(h[DSQRT_FAST], t_dsqrt_fast(y & 0x7FFFFFFFFFFFFFFF));
        h[DDIV_FAST] = mix(h[DDIV_FAST], b(ddiv_fast(dx, dy)));
        h[DFMA] = mix(h[DFMA], b(fma(dx, dy, d(z))));
        h[DCMP_C] = mix(h[DCMP_C], compare(dx, dy));
        h[DCMP_RAW] = mix(h[DCMP_RAW], t_dcmp(x, y) >> 28);
        h[DCLASS] = mix(h[DCLASS], t_dclassify(x));
        h[D2IZ] = mix(h[D2IZ], (uint32_t)(int32_t)dx);
        h[D2UIZ] = mix(h[D2UIZ], (uint32_t)dx);
        h[I2D] = mix(h[I2D], b((double)(int32_t)n));
        h[UI2D] = mix(h[UI2D], b((double)n));
        h[D2F] = mix(h[D2F], t_d2f(x));
        h[D2I_R] = mix(h[D2I_R], t_d2i_r(x));
        h[D2UI_R] = mix(h[D2UI_R], t_d2ui_r(x));
        h[FADD] = mix(h[FADD], t_fadd(fx, fy));
        h[FSUB] = mix(h[FSUB], t_fsub(fx, fy));
        h[FMUL] = mix(h[FMUL], t_fmul(fx, fy));
        h[FDIV] = mix(h[FDIV], t_fdiv(fx, fy));
        h[FDIV_FAST] = mix(h[FDIV_FAST], t_fdiv_fast(fx, fy));
        h[FSQRT] = mix(h[FSQRT], t_fsqrt(fx));
        h[FSQRT_FAST] = mix(h[FSQRT_FAST], t_fsqrt_fast(fx));
        h[FCMP] = mix(h[FCMP], t_fcmp(fx, fy) >> 28);
        h[FXPROD] = mix(h[FXPROD], t_fxprod(fx, fy));
        h[F2D] = mix(h[F2D], t_f2d(fx));
        h[I2F] = mix(h[I2F], t_i2f(n));
        h[UI2F] = mix(h[UI2F], t_ui2f(n));
        h[F2I] = mix(h[F2I], t_f2i(fx));
        h[F2UI] = mix(h[F2UI], t_f2ui(fx));
        h[F2I_R] = mix(h[F2I_R], t_f2i_r(fx));
        h[F2UI_R] = mix(h[F2UI_R], t_f2ui_r(fx));
    }
    cancel_repeating_timer(&timer);
    for (int k = 0; k < OPS; k++) printf("%s %08lx%08lx\n", NAMES[k], (unsigned long)(h[k] >> 32), (unsigned long)h[k]);

    uint32_t lo = multicore_fifo_pop_blocking();
    uint32_t hi = multicore_fifo_pop_blocking();
    printf("core1 %08lx%08lx\n", (unsigned long)hi, (unsigned long)lo);
    printf("interrupts %s, %s inside a sequence, %lu wrong\n", irqs > 100 ? "many" : "few", inside > 0 ? "some" : "none",
        (unsigned long)wrong);

    volatile double third = 1.0, two = 2.0, pi = 3.141592653589793;
    printf("%f %f %f\n", pi, -0.1 * third, 123456789.125 * third);
    printf("%.17g %.17g\n", third / 3.0, sqrt(two));
    printf("%e %e %g\n", 1e300 * third, 2.2250738585072014e-308 * third, 1e-5 * third);
    printf("%.3f %.0f %.0f %f\n", 2.0005 * third, 2.5 * third, 3.5 * third, (1e15 + 0.3) * third);
    uart_tx_wait_blocking(uart0);
    __breakpoint();
}
