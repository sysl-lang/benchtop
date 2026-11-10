// The Pico Breadboard Kit's screen and touch panel (boards/pico-breadboard-kit.hocon), driven as a program
// on the real kit drives them. The ST7796 is on SPI0 (SCK GP2, TX GP3) with its chip select on GP5, D/C
// on GP6 and reset on GP7: the program resets it, wakes it in 16-bit colour, fills the glass and draws a
// title bar and four colour blocks, then prints "ready". The GT911 is on I2C0 (SDA GP8, SCL GP9) at
// 0x5d: the program prints its product ID, then polls its status register every 10 ms, and for each
// touch reported prints "touch x y" and draws a white square under the finger. Built for the Pico and
// the Pico 2 from this one file (examples/pico/CMakeLists.txt, examples/pico2/CMakeLists.txt).
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"

#define PIN_SCK 2
#define PIN_TX 3
#define PIN_CS 5
#define PIN_DC 6
#define PIN_RESET 7
#define PIN_SDA 8
#define PIN_SCL 9

#define WIDTH 320
#define HEIGHT 480
#define GT911 0x5d

// RGB565 colours.
#define BACKGROUND 0x10a2
#define TITLE 0x2b5f
#define WHITE 0xffff
static const uint16_t blocks[4] = {0xf800, 0x07e0, 0x001f, 0xffe0};

// A command and its parameters, the chip selected for both.
static void command(uint8_t c, const uint8_t *args, size_t n) {
    gpio_put(PIN_CS, 0);
    gpio_put(PIN_DC, 0);
    spi_write_blocking(spi0, &c, 1);
    if (n > 0) {
        gpio_put(PIN_DC, 1);
        spi_write_blocking(spi0, args, n);
    }
    gpio_put(PIN_CS, 1);
}

// A `w` by `h` rectangle at (x, y) in `colour`, a row at a time.
static void fill(int x, int y, int w, int h, uint16_t colour) {
    static uint8_t row[2 * WIDTH];
    const uint8_t columns[4] = {x >> 8, x & 0xff, (x + w - 1) >> 8, (x + w - 1) & 0xff};
    const uint8_t rows[4] = {y >> 8, y & 0xff, (y + h - 1) >> 8, (y + h - 1) & 0xff};
    command(0x2a, columns, 4);
    command(0x2b, rows, 4);
    for (int i = 0; i < w; i++) {
        row[2 * i] = colour >> 8;
        row[2 * i + 1] = colour & 0xff;
    }
    uint8_t ramwr = 0x2c;
    gpio_put(PIN_CS, 0);
    gpio_put(PIN_DC, 0);
    spi_write_blocking(spi0, &ramwr, 1);
    gpio_put(PIN_DC, 1);
    for (int j = 0; j < h; j++) spi_write_blocking(spi0, row, 2 * w);
    gpio_put(PIN_CS, 1);
}

static void screen_init(void) {
    spi_init(spi0, 62500000);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_TX, GPIO_FUNC_SPI);
    gpio_init(PIN_CS);
    gpio_init(PIN_DC);
    gpio_init(PIN_RESET);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_set_dir(PIN_RESET, GPIO_OUT);
    gpio_put(PIN_CS, 1);
    gpio_put(PIN_RESET, 0);
    sleep_ms(10);
    gpio_put(PIN_RESET, 1);
    sleep_ms(120);
    command(0x11, NULL, 0);
    sleep_ms(120);
    const uint8_t colmod = 0x55;
    command(0x3a, &colmod, 1);
    const uint8_t madctl = 0x00;
    command(0x36, &madctl, 1);
    // The kit's IPS glass inverts what it is sent, so the program inverts it back.
    command(0x21, NULL, 0);
    command(0x29, NULL, 0);
}

// `n` bytes from the GT911's register `reg` on; false where it does not answer.
static bool touch_read(uint16_t reg, uint8_t *into, size_t n) {
    const uint8_t address[2] = {reg >> 8, reg & 0xff};
    if (i2c_write_blocking(i2c0, GT911, address, 2, true) != 2) return false;
    return i2c_read_blocking(i2c0, GT911, into, n, false) == (int)n;
}

static void touch_write(uint16_t reg, uint8_t value) {
    const uint8_t bytes[3] = {reg >> 8, reg & 0xff, value};
    i2c_write_blocking(i2c0, GT911, bytes, 3, false);
}

int main(void) {
    stdio_init_all();
    screen_init();
    fill(0, 0, WIDTH, HEIGHT, BACKGROUND);
    fill(0, 0, WIDTH, 40, TITLE);
    for (int i = 0; i < 4; i++) fill(20 + 75 * i, 70, 55, 55, blocks[i]);
    i2c_init(i2c0, 400000);
    gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_SDA);
    gpio_pull_up(PIN_SCL);
    uint8_t id[4] = {0};
    if (touch_read(0x8140, id, 4)) printf("GT911 id %c%c%c\n", id[0], id[1], id[2]);
    else printf("GT911 absent\n");
    printf("ready\n");
    for (;;) {
        uint8_t status = 0;
        if (touch_read(0x814e, &status, 1) && (status & 0x80)) {
            if ((status & 0x0f) > 0) {
                uint8_t p[4];
                if (touch_read(0x8150, p, 4)) {
                    int x = p[0] | (p[1] << 8);
                    int y = p[2] | (p[3] << 8);
                    printf("touch %d %d\n", x, y);
                    int left = x < 4 ? 0 : (x > WIDTH - 5 ? WIDTH - 8 : x - 4);
                    int top = y < 4 ? 0 : (y > HEIGHT - 5 ? HEIGHT - 8 : y - 4);
                    fill(left, top, 8, 8, WHITE);
                }
            }
            touch_write(0x814e, 0);
        }
        sleep_ms(10);
    }
}
