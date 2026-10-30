// USB disconnect and reconnect on a Pico 2: the SDK's stdio_usb, with stdio on UART0 as well. The test
// plugs benchtop's own USB host in (chip/usb_host_tests.sysl), which enumerates the device each time
// it connects.
//
// 1. Waits until the host has configured the device and raised DTR, and prints "first" to both.
// 2. Takes the device off the bus with tud_disconnect (its pull-up off), waits 50 ms, and puts it back
//    with tud_connect.
// 3. Waits for the host's bus reset to unmount it, then to be configured and opened again, prints "second" to both, and stops at a breakpoint once
//    the line has had time to leave.
#include <stdio.h>
#include "pico/stdlib.h"
#include "tusb.h"

static void wait_open(void) {
    while (!tud_mounted() || !tud_cdc_connected()) sleep_ms(1);
}

int main(void) {
    stdio_init_all();
    printf("up\n");
    wait_open();
    printf("first\n");
    sleep_ms(20);
    tud_disconnect();
    printf("off the bus\n");
    sleep_ms(50);
    tud_connect();
    // TinyUSB keeps the old configuration until the host's bus reset tells it otherwise.
    while (tud_mounted()) sleep_ms(1);
    wait_open();
    printf("second\n");
    sleep_ms(20);
    for (;;) __breakpoint();
}
