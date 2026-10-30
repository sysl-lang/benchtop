// The SDK's hardware_interp and multicore doorbells on both cores. Core 0 prints what its interpolators
// answer in each mode the datasheet describes (a counter, blend unsigned and signed, clamp, the RP2350's
// rotate, FORCE_MSB, ADD_RAW, CROSS_INPUT and CROSS_RESULT, save and restore). It then launches core 1;
// both count on their own INTERP0 at the same time, each with its own base, and core 1 reports what it
// saw of core 0's settings (none). Last, the doorbells: core 0 rings one on itself and one on core 1,
// whose SIO_IRQ_BELL handler acknowledges it and rings core 0 back. The program ends on a BKPT.

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/interp.h"
#include "hardware/irq.h"
#include "hardware/structs/sio.h"

#define COUNTS 16

static volatile uint32_t rung[2];

// One handler for both cores: SIO_IRQ_BELL is per core, the vector table shared.
static void bell(void) {
    uint core = get_core_num();
    uint32_t in = sio_hw->doorbell_in_clr;
    sio_hw->doorbell_in_clr = in;
    rung[core] |= in;
    // Core 1 answers on core 0's doorbell 7.
    if (core == 1) multicore_doorbell_set_other_core(7);
}

// Counts on `interp`'s lane 0 by `step`, COUNTS times; answers how many pops were wrong.
static uint32_t count(interp_hw_t *interp, uint32_t step) {
    interp_config c = interp_default_config();
    interp_set_config(interp, 0, &c);
    interp_set_accumulator(interp, 0, 0);
    interp_set_base(interp, 0, step);
    uint32_t bad = 0;
    for (uint32_t i = 1; i <= COUNTS; i++)
        if (interp_pop_lane_result(interp, 0) != step * i) bad++;
    return bad;
}

static void core1_main(void) {
    // What core 1's INTERP0 holds before it touches it: core 0's settings are core 0's own.
    multicore_fifo_push_blocking(interp_get_base(interp0, 2));
    multicore_fifo_push_blocking(interp_get_accumulator(interp0, 1));
    multicore_fifo_pop_blocking();
    multicore_fifo_push_blocking(count(interp0, 3));
    multicore_fifo_push_blocking(interp_get_accumulator(interp0, 0));
    irq_set_enabled(SIO_IRQ_BELL, true);
    multicore_fifo_push_blocking(0xb0);
    for (;;) __wfi();
}

int main(void) {
    stdio_init_all();
    for (uint lane = 0; lane < 2; lane++) {
        interp_claim_lane(interp0, lane);
        interp_claim_lane(interp1, lane);
    }

    // A counter: lane 0 with no shift and the whole word, BASE0 added on each POP.
    printf("count by 7:");
    interp_config c = interp_default_config();
    interp_set_config(interp0, 0, &c);
    interp_set_accumulator(interp0, 0, 0);
    interp_set_base(interp0, 0, 7);
    for (int i = 0; i < 5; i++) printf(" %lu", (unsigned long)interp_pop_lane_result(interp0, 0));
    printf("\n");

    // Blend: lane 1's 8 low bits are alpha; PEEK1 is BASE0 + alpha/256 of the way to BASE1, PEEK0 alpha.
    static const uint32_t alphas[] = {0, 64, 128, 192, 255};
    interp_config b0 = interp_default_config();
    interp_config_set_blend(&b0, true);
    interp_set_config(interp0, 0, &b0);
    interp_config b1 = interp_default_config();
    interp_config_set_mask(&b1, 0, 7);
    interp_set_config(interp0, 1, &b1);
    interp_set_base(interp0, 0, 500);
    interp_set_base(interp0, 1, 1000);
    printf("blend 500..1000:");
    for (int i = 0; i < 5; i++) {
        interp_set_accumulator(interp0, 1, alphas[i]);
        printf(" %lu", (unsigned long)interp_peek_lane_result(interp0, 1));
    }
    printf(" alpha %lu\n", (unsigned long)interp_peek_lane_result(interp0, 0));
    interp_config_set_signed(&b1, true);
    interp_set_config(interp0, 1, &b1);
    interp_set_base(interp0, 0, (uint32_t)-1000);
    printf("signed blend -1000..1000:");
    for (int i = 0; i < 5; i++) {
        interp_set_accumulator(interp0, 1, alphas[i]);
        printf(" %ld", (long)(int32_t)interp_peek_lane_result(interp0, 1));
    }
    printf("\n");

    // Clamp on INTERP1, signed: lane 0's value held between BASE0 and BASE1, nothing added.
    static const int32_t values[] = {-20, -10, 0, 10, 20};
    interp_config k = interp_default_config();
    interp_config_set_clamp(&k, true);
    interp_config_set_signed(&k, true);
    interp_set_config(interp1, 0, &k);
    interp_set_base(interp1, 0, (uint32_t)-10);
    interp_set_base(interp1, 1, 10);
    printf("clamp -10..10:");
    for (int i = 0; i < 5; i++) {
        interp_set_accumulator(interp1, 0, (uint32_t)values[i]);
        printf(" %ld", (long)(int32_t)interp_peek_lane_result(interp1, 0));
    }
    printf("\n");

    // The RP2350 rotates: a shift of 31 moves bit 31 to bit 0 and bit 0 to bit 1. FORCE_MSB 2 sets
    // bit 29 of what the bus reads; ADD_RAW adds the accumulator unshifted; ACCUM0_ADD reads the raw value.
    interp_config r = interp_default_config();
    interp_config_set_shift(&r, 31);
    interp_set_config(interp1, 0, &r);
    interp_set_base(interp1, 0, 0);
    interp_set_accumulator(interp1, 0, 0x80000003);
    printf("rotate 31: %08lx", (unsigned long)interp_peek_lane_result(interp1, 0));
    interp_config f = interp_default_config();
    interp_config_set_shift(&f, 4);
    interp_config_set_mask(&f, 0, 3);
    interp_config_set_force_bits(&f, 2);
    interp_set_config(interp1, 1, &f);
    interp_set_accumulator(interp1, 1, 0x123);
    interp_set_base(interp1, 1, 0x1000);
    printf(" force %08lx raw %lx", (unsigned long)interp_peek_lane_result(interp1, 1), (unsigned long)interp_get_raw(interp1, 1));
    interp_config_set_add_raw(&f, true);
    interp_set_config(interp1, 1, &f);
    printf(" add_raw %08lx\n", (unsigned long)interp_peek_lane_result(interp1, 1));

    // Cross: each lane's result into the other's accumulator on POP; lane 1 reading lane 0's accumulator.
    interp_config x0 = interp_default_config();
    interp_config_set_cross_result(&x0, true);
    interp_config x1 = interp_default_config();
    interp_config_set_cross_result(&x1, true);
    interp_set_config(interp0, 0, &x0);
    interp_set_config(interp0, 1, &x1);
    interp_set_base(interp0, 0, 10);
    interp_set_base(interp0, 1, 20);
    interp_set_base(interp0, 2, 0x77);
    interp_set_accumulator(interp0, 0, 1);
    interp_set_accumulator(interp0, 1, 2);
    uint32_t full = interp_pop_full_result(interp0);
    printf("cross result: full %lu accum %lu %lu", (unsigned long)full, (unsigned long)interp_get_accumulator(interp0, 0),
        (unsigned long)interp_get_accumulator(interp0, 1));
    interp_config_set_cross_result(&x1, false);
    interp_config_set_cross_input(&x1, true);
    interp_set_config(interp0, 1, &x1);
    printf(", cross input: lane 1 %lu\n", (unsigned long)interp_peek_lane_result(interp0, 1));

    // Save, clobber, restore.
    interp_hw_save_t saved;
    interp_save(interp0, &saved);
    interp_set_base(interp0, 0, 0);
    interp_set_accumulator(interp0, 0, 0xdead);
    interp_restore(interp0, &saved);
    printf("restored: base0 %lu accum0 %lu\n", (unsigned long)interp_get_base(interp0, 0), (unsigned long)interp_get_accumulator(interp0, 0));

    // Both cores at once, each on its own INTERP0.
    irq_set_exclusive_handler(SIO_IRQ_BELL, bell);
    multicore_launch_core1(core1_main);
    uint32_t seen_base2 = multicore_fifo_pop_blocking();
    uint32_t seen_accum1 = multicore_fifo_pop_blocking();
    printf("core 1 sees its own INTERP0: base2 %lu accum1 %lu\n", (unsigned long)seen_base2, (unsigned long)seen_accum1);
    multicore_fifo_push_blocking(1);
    uint32_t bad0 = count(interp0, 5);
    uint32_t bad1 = multicore_fifo_pop_blocking();
    uint32_t end1 = multicore_fifo_pop_blocking();
    printf("both counting: core 0 %lu wrong, ends %lu; core 1 %lu wrong, ends %lu\n", (unsigned long)bad0,
        (unsigned long)interp_get_accumulator(interp0, 0), (unsigned long)bad1, (unsigned long)end1);

    // Doorbells: set and clear on this core with the interrupt off, then ring itself and core 1.
    multicore_fifo_pop_blocking();
    multicore_doorbell_set_current_core(1);
    bool set = multicore_doorbell_is_set_current_core(1);
    multicore_doorbell_clear_current_core(1);
    printf("doorbell 1 on core 0: set %d, cleared %d\n", set, !multicore_doorbell_is_set_current_core(1));
    irq_set_enabled(SIO_IRQ_BELL, true);
    multicore_doorbell_set_current_core(3);
    while (!rung[0]) tight_loop_contents();
    printf("core 0 rang itself: 0x%02lx\n", (unsigned long)rung[0]);
    rung[0] = 0;
    multicore_doorbell_set_other_core(5);
    while (!rung[0]) tight_loop_contents();
    printf("core 1 heard 0x%02lx and rang back 0x%02lx\n", (unsigned long)rung[1], (unsigned long)rung[0]);
    printf("pending: core 0 0x%02lx core 1 0x%02lx\n", (unsigned long)sio_hw->doorbell_in_set, (unsigned long)sio_hw->doorbell_out_set);
    __breakpoint();
}
