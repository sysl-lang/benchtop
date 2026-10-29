// What the start-up blocks report, over UART0: the SDK's idea of each clock's frequency against the
// frequency counter's reading of it, the chip identity from SYSINFO, the power-on state machine, the
// ring oscillator's random bits and the voltage regulator after a vreg_set_voltage.

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/structs/powman.h"
#include "hardware/structs/psm.h"
#include "hardware/structs/rosc.h"
#include "hardware/structs/sysinfo.h"
#include "hardware/vreg.h"

static void clock_line(const char *name, uint clk, uint src) {
    printf("%s hz=%u khz=%u\n", name, (unsigned)clock_get_hz(clk), (unsigned)frequency_count_khz(src));
}

int main(void) {
    stdio_init_all();

    // clk_hstx is not set up by the SDK's start-up: run it from PLL_SYS over 2.
    clock_configure(clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, clock_get_hz(clk_sys), clock_get_hz(clk_sys) / 2);

    printf("chip_id %08x\n", (unsigned)sysinfo_hw->chip_id);
    printf("package_sel %u platform %u\n", (unsigned)sysinfo_hw->package_sel, (unsigned)sysinfo_hw->platform);
    printf("psm_done %07x\n", (unsigned)psm_hw->done);

    clock_line("clk_ref", clk_ref, CLOCKS_FC0_SRC_VALUE_CLK_REF);
    clock_line("clk_sys", clk_sys, CLOCKS_FC0_SRC_VALUE_CLK_SYS);
    clock_line("clk_peri", clk_peri, CLOCKS_FC0_SRC_VALUE_CLK_PERI);
    clock_line("clk_usb", clk_usb, CLOCKS_FC0_SRC_VALUE_CLK_USB);
    clock_line("clk_adc", clk_adc, CLOCKS_FC0_SRC_VALUE_CLK_ADC);
    clock_line("clk_hstx", clk_hstx, CLOCKS_FC0_SRC_VALUE_CLK_HSTX);
    printf("pll_sys khz=%u\n", (unsigned)frequency_count_khz(CLOCKS_FC0_SRC_VALUE_PLL_SYS_CLKSRC_PRIMARY));
    printf("pll_usb khz=%u\n", (unsigned)frequency_count_khz(CLOCKS_FC0_SRC_VALUE_PLL_USB_CLKSRC_PRIMARY));
    printf("xosc khz=%u\n", (unsigned)frequency_count_khz(CLOCKS_FC0_SRC_VALUE_XOSC_CLKSRC));
    printf("rosc khz=%u\n", (unsigned)frequency_count_khz(CLOCKS_FC0_SRC_VALUE_ROSC_CLKSRC));
    printf("lposc khz=%u\n", (unsigned)frequency_count_khz(CLOCKS_FC0_SRC_VALUE_LPOSC_CLKSRC));

    unsigned bits = 0;
    for (int i = 0; i < 32; i++) bits = (bits << 1) | (rosc_hw->randombit & 1u);
    printf("rosc_random %08x\n", bits);

    vreg_set_voltage(VREG_VOLTAGE_1_10);
    printf("vreg vsel %u\n", (unsigned)((powman_hw->vreg & POWMAN_VREG_VSEL_BITS) >> POWMAN_VREG_VSEL_LSB));
    printf("done\n");
    for (;;) tight_loop_contents();
}
