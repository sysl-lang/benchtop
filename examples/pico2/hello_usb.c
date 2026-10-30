/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// pico-examples' hello_world/usb (commit 0d62f75), unchanged: stdio over USB only (the SDK's stdio_usb,
// TinyUSB's CDC ACM device), "Hello, world!" once a second for ever.

#include <stdio.h>
#include "pico/stdlib.h"

int main() {
    stdio_init_all();
    while (true) {
        printf("Hello, world!\n");
        sleep_ms(1000);
    }
}
