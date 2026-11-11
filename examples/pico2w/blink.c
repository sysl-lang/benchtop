// The Pico 2 W's LED blink: the LED hangs off the CYW43439, not an RP2350 GPIO, so it is driven through
// cyw43_arch, whose first call brings the radio up over the PIO's gSPI: the bus, the firmware and NVRAM
// upload, then the control channel. Console on UART0.

#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

int main(void) {
    stdio_init_all();
    printf("pico2w blink\n");

    int err = cyw43_arch_init();
    printf("cyw43_arch_init %d\n", err);
    if (err) {
        return 1;
    }

    for (unsigned i = 0;; i++) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
        sleep_ms(250);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
        sleep_ms(250);
        printf("blink %u\n", i);
    }
}
