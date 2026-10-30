// UART0 and UART1 talking over two wires the test puts on the workbench: GPIO 12, UART0's TX as well as
// GPIO 0, to GPIO 5, UART1's RX; and GPIO 4, UART1's TX, to GPIO 13, UART0's RX as well as GPIO 1. stdio
// stays on UART0, so what UART0 sends reaches the console and UART1 alike. Each step prints what it saw:
// 1. UART1 hears "ping" as the console prints it, and its receive timeout raises UART1_IRQ (six
//    characters, under its FIFO level), timed by the 1 µs timer.
// 2. UART1 sends "pong", which UART0 reads.
// 3. UART1 holds a break, which UART0 reads as a 0 with BE and FE.
// 4. UART1 sends 40 characters UART0 does not read: 32 fill its FIFO, and the rest are an overrun.
// Then a breakpoint, with the last line still in UART0's FIFO.

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/uart.h"

static volatile int uart1_irqs;
static volatile uint32_t uart1_mis;

static void on_uart1(void) {
    uart1_mis = uart_get_hw(uart1)->mis;
    uart1_irqs++;
    uart_get_hw(uart1)->imsc = 0;
}

int main() {
    stdio_init_all();
    printf("uart loopback\n");
    uart_tx_wait_blocking(uart0);

    uart_init(uart1, 115200);
    gpio_set_function(4, GPIO_FUNC_UART);
    gpio_set_function(5, GPIO_FUNC_UART);
    gpio_set_function(12, GPIO_FUNC_UART);
    gpio_set_function(13, GPIO_FUNC_UART);

    irq_set_exclusive_handler(UART1_IRQ, on_uart1);
    irq_set_enabled(UART1_IRQ, true);
    uart_get_hw(uart1)->imsc = UART_UARTIMSC_RTIM_BITS;
    uint64_t t0 = time_us_64();
    printf("ping\n");
    while (!uart1_irqs) tight_loop_contents();
    uint64_t t1 = time_us_64();
    char got[48];
    int n = 0;
    while (uart_is_readable(uart1) && n < 47) got[n++] = uart_getc(uart1);
    got[n] = 0;
    printf("uart1 heard %d: %c%c%c%c 0x%02x 0x%02x, mis 0x%03x after %u us\n", n, got[0], got[1], got[2],
           got[3], got[4], got[5], (unsigned)uart1_mis, (unsigned)(t1 - t0));

    uart_puts(uart1, "pong");
    for (int i = 0; i < 4; i++) got[i] = uart_getc(uart0);
    got[4] = 0;
    printf("uart0 heard %s\n", got);

    uart_set_break(uart1, true);
    busy_wait_us(200);
    uart_set_break(uart1, false);
    while (!uart_is_readable(uart0)) tight_loop_contents();
    uint32_t dr = uart_get_hw(uart0)->dr;
    uint32_t rsr = uart_get_hw(uart0)->rsr;
    printf("break: dr 0x%03x rsr 0x%x errors 0x%03x\n", (unsigned)dr, (unsigned)rsr,
           (unsigned)(uart_get_hw(uart0)->ris & 0x780));
    uart_get_hw(uart0)->rsr = 0;
    uart_get_hw(uart0)->icr = 0x7ff;

    for (int i = 0; i < 40; i++) uart_putc_raw(uart1, 'a' + i % 26);
    uart_tx_wait_blocking(uart1);
    rsr = uart_get_hw(uart0)->rsr;
    uint32_t ris = uart_get_hw(uart0)->ris;
    n = 0;
    while (uart_is_readable(uart0)) {
        got[n % 48] = uart_getc(uart0);
        n++;
    }
    printf("overrun: read %d, last %c, rsr 0x%x, oe %d\n", n, got[(n - 1) % 48], (unsigned)rsr,
           (int)((ris & UART_UARTRIS_OERIS_BITS) != 0));
    printf("done\n");
    __breakpoint();
}
