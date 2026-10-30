// GPIO 25, the Pico's green LED, toggled every 250 ms.

#include "pico/stdlib.h"

#define LED 25

int main(void) {
    gpio_init(LED);
    gpio_set_dir(LED, GPIO_OUT);
    bool on = false;
    for (;;) {
        on = !on;
        gpio_put(LED, on);
        sleep_ms(250);
    }
}
