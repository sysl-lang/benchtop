// benchtop's own: the PIO programs the SDK's pioasm assembled into pio/*.pio.h, and the state machine
// configuration each pico-examples program's `*_program_init` builds for them, as the SDK computes it
// (its `sm_config_*` calls, copied from the `% c-sdk` block of each .pio, without the pins and the
// pio_sm_init that need the PIO on the GPIO mux). One line a program: the registers pio_sm_init would
// write, then the instructions. chip/pio_tests.sysl loads them into the PIO at register level.

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hello.pio.h"
#include "ws2812.pio.h"
#include "uart_tx.pio.h"
#include "uart_rx.pio.h"
#include "rp2350_only.pio.h"

static void show(const char *name, const pio_program_t *p, pio_sm_config c) {
    printf("%s %08x %08x %08x %08x", name, (unsigned)c.clkdiv, (unsigned)c.execctrl, (unsigned)c.shiftctrl, (unsigned)c.pinctrl);
    for (uint i = 0; i < p->length; i++)
        printf(" %04x", p->instructions[i]);
    printf("\n");
}

int main() {
    stdio_init_all();

    // hello_program_init(pio, sm, 0, PICO_DEFAULT_LED_PIN)
    pio_sm_config c = hello_program_get_default_config(0);
    sm_config_set_out_pins(&c, PICO_DEFAULT_LED_PIN, 1);
    show("hello", &hello_program, c);

    // ws2812_program_init(pio, sm, 0, 2, 800000, false)
    c = ws2812_program_get_default_config(0);
    sm_config_set_sideset_pins(&c, 2);
    sm_config_set_out_shift(&c, false, true, 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    int cycles_per_bit = ws2812_T1 + ws2812_T2 + ws2812_T3;
    float div = clock_get_hz(clk_sys) / (800000 * cycles_per_bit);
    sm_config_set_clkdiv(&c, div);
    show("ws2812", &ws2812_program, c);

    // uart_tx_program_init(pio, sm, 0, 0, 115200)
    c = uart_tx_program_get_default_config(0);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_out_pins(&c, 0, 1);
    sm_config_set_sideset_pins(&c, 0);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / (8 * 115200));
    show("uart_tx", &uart_tx_program, c);

    // uart_rx_program_init(pio, sm, 0, 3, PICO_DEFAULT_UART_BAUD_RATE)
    c = uart_rx_program_get_default_config(0);
    sm_config_set_in_pins(&c, 3);
    sm_config_set_jmp_pin(&c, 3);
    sm_config_set_in_shift(&c, true, false, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / (8 * PICO_DEFAULT_UART_BAUD_RATE));
    show("uart_rx", &uart_rx_program, c);

    // The RP2350-only pair: the table's OUT pins 8 to 11 and JMP pin 6; the follower's SET pin 12.
    c = table_program_get_default_config(0);
    sm_config_set_out_pins(&c, 8, 4);
    sm_config_set_jmp_pin(&c, 6);
    show("table", &table_program, c);
    c = follower_program_get_default_config(0);
    sm_config_set_set_pins(&c, 12, 1);
    show("follower", &follower_program, c);
    return 0;
}
