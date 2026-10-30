// The GPIO interrupts through hardware_gpio, on both cores, with nothing driven from outside: GPIO 5 is
// an SIO output whose pad reads its own level back. Edges one at a time and both in one interrupt,
// a level interrupt and an input that is off reading low, a forced interrupt, IRQOVER and INOVER,
// core 1's own enables, and the dormant wake waking the ROSC from DORMANT. Each line prints what the
// SDK's callback saw, as core, GPIO and event mask.

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/structs/rosc.h"

#define LOOP_PIN 5
#define FORCED_PIN 6
#define CORE1_PIN 7

static volatile uint32_t seen[8];
static volatile uint32_t seen_count;

static void callback(uint gpio, uint32_t events) {
    seen[seen_count++ & 7] = (get_core_num() << 16) | (gpio << 8) | events;
    // A level stays asserted while the input does, so its handler turns it off.
    uint32_t levels = events & (GPIO_IRQ_LEVEL_LOW | GPIO_IRQ_LEVEL_HIGH);
    if (levels) gpio_set_irq_enabled(gpio, levels, false);
    // A force stays until it is cleared.
    if (gpio == FORCED_PIN) hw_clear_bits(&io_bank0_hw->proc0_irq_ctrl.intf[FORCED_PIN / 8], 0xfu << (4 * (FORCED_PIN % 8)));
}

static void report(const char *what) {
    busy_wait_us(20);
    printf("%s:", what);
    for (uint32_t i = 0; i < seen_count; i++) {
        uint32_t s = seen[i];
        printf(" core %lu gpio %lu 0x%lx", s >> 16, (s >> 8) & 0xff, s & 0xff);
    }
    printf("\n");
    seen_count = 0;
}

// INTR's nibble for `gpio`: LEVEL_LOW, LEVEL_HIGH, EDGE_LOW, EDGE_HIGH.
static uint32_t intr(uint gpio) {
    return (io_bank0_hw->intr[gpio / 8] >> (4 * (gpio % 8))) & 0xf;
}

static void core1_entry(void) {
    gpio_set_irq_enabled_with_callback(CORE1_PIN, GPIO_IRQ_EDGE_RISE, true, &callback);
    multicore_fifo_push_blocking(1);
    for (;;) __wfi();
}

int main() {
    stdio_init_all();
    printf("gpio irq\n");
    multicore_launch_core1(core1_entry);
    multicore_fifo_pop_blocking();
    gpio_init(LOOP_PIN);
    gpio_set_dir(LOOP_PIN, GPIO_OUT);
    gpio_put(LOOP_PIN, 0);
    gpio_init(CORE1_PIN);
    gpio_set_dir(CORE1_PIN, GPIO_OUT);
    gpio_put(CORE1_PIN, 0);
    printf("raw: gpio 5 0x%lx gpio 6 0x%lx\n", intr(LOOP_PIN), intr(FORCED_PIN));

    gpio_set_irq_enabled_with_callback(LOOP_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &callback);
    gpio_put(LOOP_PIN, 1);
    busy_wait_us(10);
    gpio_put(LOOP_PIN, 0);
    report("edges");

    irq_set_enabled(IO_IRQ_BANK0, false);
    gpio_put(LOOP_PIN, 1);
    gpio_put(LOOP_PIN, 0);
    printf("masked: ints 0x%lx\n", (io_bank0_hw->proc0_irq_ctrl.ints[0] >> 20) & 0xf);
    irq_set_enabled(IO_IRQ_BANK0, true);
    report("unmasked");

    gpio_set_irq_enabled(LOOP_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
    gpio_put(LOOP_PIN, 1);
    gpio_set_irq_enabled(LOOP_PIN, GPIO_IRQ_LEVEL_HIGH, true);
    gpio_set_irq_enabled(FORCED_PIN, GPIO_IRQ_LEVEL_LOW, true);
    report("levels");

    gpio_set_irq_enabled(FORCED_PIN, GPIO_IRQ_EDGE_RISE, true);
    hw_set_bits(&io_bank0_hw->proc0_irq_ctrl.intf[FORCED_PIN / 8], GPIO_IRQ_EDGE_RISE << (4 * (FORCED_PIN % 8)));
    report("forced");
    gpio_set_irq_enabled(FORCED_PIN, GPIO_IRQ_EDGE_RISE, false);

    gpio_acknowledge_irq(LOOP_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL);
    gpio_set_irqover(LOOP_PIN, GPIO_OVERRIDE_INVERT);
    uint32_t irqtoproc = (io_bank0_hw->io[LOOP_PIN].status & IO_BANK0_GPIO0_STATUS_IRQTOPROC_BITS) != 0;
    printf("irqover invert: irqtoproc %lu gpio_get %d raw 0x%lx\n", irqtoproc, gpio_get(LOOP_PIN), intr(LOOP_PIN));
    gpio_set_irqover(LOOP_PIN, GPIO_OVERRIDE_NORMAL);
    gpio_acknowledge_irq(LOOP_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL);
    gpio_set_inover(LOOP_PIN, GPIO_OVERRIDE_LOW);
    irqtoproc = (io_bank0_hw->io[LOOP_PIN].status & IO_BANK0_GPIO0_STATUS_IRQTOPROC_BITS) != 0;
    printf("inover low: irqtoproc %lu gpio_get %d raw 0x%lx\n", irqtoproc, gpio_get(LOOP_PIN), intr(LOOP_PIN));
    gpio_set_inover(LOOP_PIN, GPIO_OVERRIDE_NORMAL);

    gpio_put(CORE1_PIN, 1);
    while (seen_count == 0) tight_loop_contents();
    printf("gpio 7 enables: core 1 0x%lx core 0 0x%lx\n", io_bank0_hw->proc1_irq_ctrl.inte[0] >> 28, io_bank0_hw->proc0_irq_ctrl.inte[0] >> 28);
    report("core 1");

    // The dormant wake: an asserted dormant-wake interrupt brings the ROSC straight back out of DORMANT,
    // and a falling edge on GPIO 5 wakes it once it has gone dormant with nothing asserted.
    gpio_set_dormant_irq_enabled(LOOP_PIN, GPIO_IRQ_LEVEL_HIGH, true);
    rosc_hw->dormant = ROSC_DORMANT_VALUE_DORMANT;
    printf("dormant with gpio 5 high: ints 0x%lx rosc %s\n", (io_bank0_hw->dormant_wake_irq_ctrl.ints[0] >> 20) & 0xf,
        rosc_hw->dormant == ROSC_DORMANT_VALUE_WAKE ? "awake" : "dormant");
    gpio_set_dormant_irq_enabled(LOOP_PIN, GPIO_IRQ_LEVEL_HIGH, false);
    gpio_set_dormant_irq_enabled(LOOP_PIN, GPIO_IRQ_EDGE_FALL, true);
    rosc_hw->dormant = ROSC_DORMANT_VALUE_DORMANT;
    printf("dormant: rosc %s\n", rosc_hw->dormant == ROSC_DORMANT_VALUE_WAKE ? "awake" : "dormant");
    gpio_put(LOOP_PIN, 0);
    printf("gpio 5 falls: rosc %s\n", rosc_hw->dormant == ROSC_DORMANT_VALUE_WAKE ? "awake" : "dormant");
    gpio_set_dormant_irq_enabled(LOOP_PIN, GPIO_IRQ_EDGE_FALL, false);
    printf("done\n");
    return 0;
}
