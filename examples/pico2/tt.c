// The M33's test-target instructions through ACLE's <arm_cmse.h>: TT, TTT, TTA and TTAT of an SRAM
// address and a private peripheral bus address, first as the SDK leaves the SAU (disabled, ALLNS
// clear: everything Secure), then with two SAU regions — one Non-secure, one Non-secure callable —
// and the SAU enabled. Each line prints the four answers' raw words, which the architecture defines
// bit by bit (ARMv8-M ARM, "TT, TTT, TTA, TTAT").

#include <arm_cmse.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/structs/sau.h"

static void show(const char *what, void *p) {
    printf("%-10s TT %08lx TTT %08lx TTA %08lx TTAT %08lx\n", what,
           (unsigned long)cmse_TT(p).value, (unsigned long)cmse_TTT(p).value,
           (unsigned long)cmse_TTA(p).value, (unsigned long)cmse_TTAT(p).value);
}

int main(void) {
    stdio_init_all();
    void *sram = (void *)0x20040000;
    void *nsc = (void *)0x20050100;
    void *ppb = (void *)(PPB_BASE + 0xed00);
    printf("sau ctrl %lu type %lu\n", (unsigned long)sau_hw->ctrl, (unsigned long)sau_hw->type);
    show("sram", sram);
    show("ppb", ppb);
    // Region 0, 0x20040000-0x2004ffff, Non-secure; region 5, 0x20050000-0x2005ffff, Non-secure
    // callable.
    sau_hw->rnr = 0;
    sau_hw->rbar = 0x20040000;
    sau_hw->rlar = 0x2004ffe0 | 1;
    sau_hw->rnr = 5;
    sau_hw->rbar = 0x20050000;
    sau_hw->rlar = 0x2005ffe0 | 3;
    sau_hw->ctrl = 1;
    show("ns", sram);
    show("nsc", nsc);
    show("secure", (void *)0x20060000);
    show("ppb", ppb);
    for (;;) {
        __breakpoint();
    }
}
