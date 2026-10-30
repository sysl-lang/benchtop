// USB device mode on a Pico 2: the SDK's stdio_usb, which is TinyUSB's CDC ACM device with the SDK's
// reset interface beside it, and stdio on UART0 as well. The test plays the USB host
// (chip/usb_tests.sysl): it resets the bus, reads the descriptors, sets the address and the
// configuration, raises DTR, reads the CDC port and writes to it.
//
// 1. Prints the board's serial number, which the host also reads as string descriptor 3, then waits
//    until the host has configured the device, and says so on UART0.
// 2. Waits until the host raises DTR, then prints a line, to UART0 and to the CDC port.
// 3. Reads one character from the CDC port, names it on UART0 and stops at a breakpoint.
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "tusb.h"

int main(void) {
    stdio_init_all();
    // The serial number stdio_usb's string descriptor 3 carries.
    char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    pico_get_unique_board_id_string(serial, sizeof serial);
    printf("usb device up, serial %s\n", serial);
    while (!tud_mounted()) sleep_ms(1);
    printf("configured\n");
    while (!tud_cdc_connected()) sleep_ms(1);
    printf("hello over usb\n");
    int c = getchar_timeout_us(2000000);
    printf("got %d\n", c);
    for (;;) __breakpoint();
}
