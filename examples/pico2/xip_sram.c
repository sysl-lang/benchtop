// A whole program in the XIP cache used as SRAM: code, data and stack in the 16 KB at XIP_SRAM_BASE
// (0x13ffc000), as the SDK's `xip_ram` binary type links it. The loader pins the cache's lines over
// that window before the program starts, as the boot ROM does for an image whose block asks for it.
#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"

int main(void) {
    stdio_init_all();
    int here = 0;
    printf("xip_sram begins: main at %08x, stack at %08x\n", (unsigned)(uintptr_t)main, (unsigned)(uintptr_t)&here);
    for (int i = 0; i < 5; i++) {
        printf("running... %d\n", i);
        sleep_ms(100);
    }
    printf("PASSED\n");
    return 0;
}
