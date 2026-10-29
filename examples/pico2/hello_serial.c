// stdio over UART0 (GP0 TX at 115200 baud, the Pico 2's default), a numbered line every 100 ms.

#include <stdio.h>
#include "pico/stdlib.h"

int main(void) {
    stdio_init_all();
    for (int n = 0;; n++) {
        printf("hello, serial %d\n", n);
        sleep_ms(100);
    }
}
