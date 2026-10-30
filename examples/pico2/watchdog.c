// The watchdog on a Pico 2, through the SDK's hardware_watchdog API, across four boots of one flash
// image. SCRATCH0, which survives the watchdog's reset, counts the boots:
//
// 1. A clean boot feeds the watchdog three times, then starves it: the counter runs out and resets
//    the chip, and the boot ROM boots the flash image again.
// 2. watchdog_caused_reboot() and watchdog_enable_caused_reboot() are true, REASON is TIMER. It
//    reboots at once with watchdog_reboot(0, 0, 0), which is TRIGGER.
// 3. REASON is FORCE; watchdog_enable_caused_reboot() is false, since watchdog_reboot cleared
//    SCRATCH4. It asks for a reboot to `landed`, with its own stack, 10 ms on.
// 4. The boot ROM enters `landed` with no runtime: it records the boot type the ROM reports (PC_SP)
//    and watchdog_caused_reboot() (false, as the boot was not a normal one) in SCRATCH1 and SCRATCH2,
//    and stops at a breakpoint.
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

static void __attribute__((noreturn, noinline)) landed(void) {
    watchdog_hw->scratch[1] = (uint32_t)rom_get_last_boot_type();
    watchdog_hw->scratch[2] = watchdog_caused_reboot();
    for (;;) __breakpoint();
}

static const char *yes(bool b) { return b ? "yes" : "no"; }

int main(void) {
    stdio_init_all();
    uint32_t boot = watchdog_hw->scratch[0] + 1;
    watchdog_hw->scratch[0] = boot;
    printf("boot %lu: reason 0x%lx, caused %s, enable caused %s\n", (unsigned long)boot,
           (unsigned long)watchdog_hw->reason, yes(watchdog_caused_reboot()), yes(watchdog_enable_caused_reboot()));
    if (boot == 1) {
        watchdog_enable(100, true);
        for (int i = 0; i < 3; i++) {
            sleep_ms(50);
            watchdog_update();
            printf("fed %d\n", i);
        }
        printf("starving it\n");
    } else if (boot == 2) {
        watchdog_reboot(0, 0, 0);
    } else {
        watchdog_reboot((uint32_t)landed, 0x20081000, 10);
    }
    for (;;) tight_loop_contents();
}
