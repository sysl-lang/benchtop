// Firmware for Benchtop's virtual workbench, not a physical Pico.
// Change OUT to drive the LEDs; the UI changes IN when a button is held.
#include <stdint.h>
#define GPIO ((volatile uint32_t *)0x50000000u)
#define CONSOLE (*(volatile uint32_t *)0x40000000u)
extern uint32_t __stack_top;
void start(void);
__attribute__((section(".vectors"),used)) const void *vectors[48] = {
    [0]=&__stack_top, [1]=(const void *)start
};
void start(void) {
    const char *message="Workbench ready. Hold 1 or 2.\n";
    while (*message) CONSOLE=(unsigned char)*message++;
    GPIO[2]=7; // GP0..2 output; all other pins are inputs.
    uint32_t count=0, heartbeat=0;
    for (;;) {
        if (++count == 40000) { count=0; heartbeat^=1; }
        GPIO[0]=heartbeat | (((~GPIO[1] >> 8) & 3) << 1);
    }
}
