// The pin-level parts benchtop puts on an RP board's header, driven as a program on a real Pico or
// Pico 2 drives them (testdata/rp-parts/, chip/parts_tests.sysl): a button on GPIO 14 with the pad's
// pull-up, each edge printed by the SDK's GPIO callback; a seven-segment digit on GPIO 2 to 9 with its
// common cathode on GPIO 10, showing 7; an HD44780 in four-bit mode on GPIO 16 to 21 showing "Hi"; and
// a buzzer on GPIO 15 sounded by its PWM slice at 1 kHz and then 2 kHz. Built for both boards from this
// one file (examples/pico/CMakeLists.txt names it).
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"

#define BUTTON 14
#define BUZZER 15
#define SEGMENT_A 2
#define COMMON 10
#define LCD_RS 16
#define LCD_E 17
#define LCD_D4 18

static void edge(uint gpio, uint32_t events) {
    printf("GPIO %u %s\n", gpio, (events & GPIO_IRQ_EDGE_FALL) ? "down" : "up");
}

// The buzzer's PWM at `hz`, half on: the counter at 1 MHz.
static void tone(uint hz) {
    uint slice = pwm_gpio_to_slice_num(BUZZER);
    uint32_t top = 1000000 / hz;
    pwm_set_clkdiv_int_frac(slice, clock_get_hz(clk_sys) / 1000000, 0);
    pwm_set_wrap(slice, top - 1);
    pwm_set_gpio_level(BUZZER, top / 2);
    pwm_set_enabled(slice, true);
    printf("tone %u\n", hz);
}

// Four bits to the LCD, latched as E falls.
static void lcd_nibble(uint8_t n, bool data) {
    gpio_put(LCD_RS, data);
    gpio_put_masked(0xfu << LCD_D4, (uint32_t)n << LCD_D4);
    gpio_put(LCD_E, 1);
    busy_wait_us(1);
    gpio_put(LCD_E, 0);
    busy_wait_us(100);
}

static void lcd_byte(uint8_t b, bool data) {
    lcd_nibble(b >> 4, data);
    lcd_nibble(b & 0xf, data);
    busy_wait_us(2000);
}

static void lcd_init(void) {
    uint32_t pins = (1u << LCD_RS) | (1u << LCD_E) | (0xfu << LCD_D4);
    gpio_init_mask(pins);
    gpio_set_dir_out_masked(pins);
    busy_wait_ms(50);
    lcd_nibble(3, false);
    busy_wait_ms(5);
    lcd_nibble(3, false);
    busy_wait_us(200);
    lcd_nibble(3, false);
    lcd_nibble(2, false);
    lcd_byte(0x28, false);
    lcd_byte(0x0c, false);
    lcd_byte(0x01, false);
    lcd_byte(0x06, false);
}

int main() {
    stdio_init_all();
    gpio_init(BUTTON);
    gpio_pull_up(BUTTON);
    busy_wait_us(10);
    printf("button idle %d\n", gpio_get(BUTTON));
    gpio_set_irq_enabled_with_callback(BUTTON, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &edge);

    // 7: segments a, b and c lit, the common driven low.
    uint32_t digit = 0x1ffu << SEGMENT_A;
    gpio_init_mask(digit);
    gpio_set_dir_out_masked(digit);
    gpio_put_masked(digit, 0x07u << SEGMENT_A);
    printf("digit 7\n");

    lcd_init();
    lcd_byte('H', true);
    lcd_byte('i', true);
    printf("lcd Hi\n");

    gpio_set_function(BUZZER, GPIO_FUNC_PWM);
    tone(1000);
    sleep_ms(20);
    tone(2000);
    for (;;) __wfi();
}
