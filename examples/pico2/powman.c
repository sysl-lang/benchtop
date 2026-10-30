// POWMAN's low-power states on a Pico 2, through the SDK's hardware_powman, across three power-ups of
// one flash image. POWMAN's SCRATCH0, in the always-on domain, counts them; `kept`, a word in SRAM bank
// 0 that crt0 neither copies nor clears, shows whether the state slept in kept that bank.
//
// 1. Power-on. Two requests the power manager turns down: P0 with the XIP cache off (invalid:
//    BAD_SW_REQ), and P1.7 while a level power-up request on GPIO 15, pulled up, is asserted
//    (REQ_IGNORED). Then the always-on timer from the LPOSC, an alarm wake-up 1 s ahead, and P1.7:
//    every domain off.
// 2. The alarm powered the switched core up (LAST_SWCORE_PWRUP bit 6, CHIP_RESET.HAD_SWCORE_PD), `kept`
//    reads 0 as SRAM was off, and the timer has passed the alarm. BOOTDIS.NEXT is set and BOOT0 given a
//    value, a rising edge on GPIO 14 is the wake-up (the test drives it from outside the chip), and the
//    state keeps SRAM bank 0.
// 3. GPIO 14 powered it up (bit 1, PWRUP0); `kept` survived; BOOTDIS.NOW made the boot ROM ignore BOOT0
//    and was cleared; PWRUP0's edge is still latched until written 1. A breakpoint ends it.
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "hardware/powman.h"
#include "hardware/structs/powman.h"

#define MAGIC 0xc0ffeeu
// P0.0: every domain on (a set bit is a domain on in the SDK's powman_power_state).
#define ALL_ON 0xfu

static uint32_t __uninitialized_ram(kept);

static void sleep_now(powman_power_state state, const char *until) {
    // The SDK's pico_low_power does the same: the wake-up state is every domain on, and BOOT0-3 left
    // as the boot ROM should see them.
    bool valid = powman_configure_wakeup_state(state, ALL_ON);
    int rc = powman_set_power_state(state);
    printf("sleeping until %s: valid %d rc %d state 0x%lx\n", until, valid, rc, (unsigned long)powman_hw->state);
    for (;;) __wfi();
}

int main(void) {
    stdio_init_all();
    uint32_t boot = powman_hw->scratch[0] + 1;
    powman_hw->scratch[0] = boot;
    printf("boot %lu: state 0x%lx last_swcore_pwrup 0x%lx had_swcore_pd %d\n", (unsigned long)boot,
           (unsigned long)powman_hw->state, (unsigned long)powman_hw->last_swcore_pwrup,
           (powman_hw->chip_reset & POWMAN_CHIP_RESET_HAD_SWCORE_PD_BITS) != 0);
    if (boot == 1) {
        // P0 with the XIP cache down: the switched core cannot run without it.
        powman_power_state no_xip = powman_power_state_with_domain_off(ALL_ON, POWMAN_POWER_DOMAIN_XIP_CACHE);
        int rc = powman_set_power_state(no_xip);
        printf("xip off: rc %d state 0x%lx\n", rc, (unsigned long)powman_hw->state);
        gpio_pull_up(15);
        powman_enable_gpio_wakeup(0, 15, false, true);
        rc = powman_set_power_state(POWMAN_POWER_STATE_NONE);
        printf("gpio 15 high: rc %d state 0x%lx current_pwrup_req 0x%lx\n", rc, (unsigned long)powman_hw->state,
               (unsigned long)powman_hw->current_pwrup_req);
        powman_disable_gpio_wakeup(0);
        powman_timer_set_1khz_tick_source_lposc_with_hz(32768);
        powman_timer_set_ms(0);
        powman_timer_start();
        uint64_t alarm = powman_timer_get_ms() + 1000;
        powman_hw->scratch[1] = (uint32_t)alarm;
        powman_enable_alarm_wakeup_at_ms(alarm);
        kept = MAGIC;
        sleep_now(POWMAN_POWER_STATE_NONE, "the alarm");
    }
    printf("kept 0x%lx\n", (unsigned long)kept);
    if (boot == 2) {
        printf("timer past the alarm: %d\n", powman_timer_get_ms() >= powman_hw->scratch[1]);
        powman_disable_alarm_wakeup();
        powman_set_bits(&powman_hw->bootdis, POWMAN_BOOTDIS_NEXT_BITS);
        powman_hw->boot[0] = 0x12345678;
        powman_enable_gpio_wakeup(0, 14, true, true);
        kept = MAGIC;
        sleep_now(powman_power_state_with_domain_on(POWMAN_POWER_STATE_NONE, POWMAN_POWER_DOMAIN_SRAM_BANK0), "gpio 14 rises");
    }
    printf("bootdis 0x%lx boot0 0x%lx pwrup0 status %d\n", (unsigned long)powman_hw->bootdis,
           (unsigned long)powman_hw->boot[0], (powman_hw->pwrup[0] & POWMAN_PWRUP0_STATUS_BITS) != 0);
    printf("done\n");
    for (;;) __breakpoint();
}
