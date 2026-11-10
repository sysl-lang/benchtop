// A clock on a Pico 2 (testdata/rp-parts/pico2-oled-clock.hocon): a DS3231 on I2C0 (SDA GP4, SCL GP5)
// and a 128x64 SSD1306 on SPI0 (SCK GP18, MOSI GP19) with its chip select on GP17, D/C on GP20 and reset
// on GP21. The program resets and wakes the OLED, says "ready", then reads the DS3231's time and date
// registers every 50 ms; each time the seconds change it prints "time HH:MM:SS 20YY-MM-DD" and draws the
// time large and the date small inside a frame.
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"

#define PIN_SDA 4
#define PIN_SCL 5
#define PIN_CS 17
#define PIN_SCK 18
#define PIN_MOSI 19
#define PIN_DC 20
#define PIN_RESET 21

#define DS3231 0x68

// The OLED's memory as it is sent: eight pages of 128 columns, a byte a column, bit n row n of the page.
static uint8_t frame[8 * 128];

// The digits, ':' and '-' of a 5x7 font, a byte a column, bit n row n.
static const uint8_t glyphs[12][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00}, {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4B, 0x31}, {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1E}, {0x00, 0x36, 0x36, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08},
};

// Bytes to the OLED, D/C low for commands and high for data, the chip selected for them.
static void oled_send(bool data, const uint8_t *bytes, size_t n) {
    gpio_put(PIN_DC, data);
    gpio_put(PIN_CS, 0);
    spi_write_blocking(spi0, bytes, n);
    gpio_put(PIN_CS, 1);
}

static void oled_init(void) {
    spi_init(spi0, 8000000);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    const uint pins[3] = {PIN_CS, PIN_DC, PIN_RESET};
    for (int i = 0; i < 3; i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_OUT);
        gpio_put(pins[i], 1);
    }
    gpio_put(PIN_RESET, 0);
    sleep_ms(1);
    gpio_put(PIN_RESET, 1);
    // Display off, clock, 64 rows, no offset, start line 0, charge pump on, horizontal addressing,
    // unflipped, COM pins, contrast, precharge, VCOMH, follow RAM, not inverted, display on.
    static const uint8_t setup[] = {0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00,
        0xA0, 0xC0, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF};
    oled_send(false, setup, sizeof setup);
}

// The whole frame, from column 0 of page 0.
static void oled_show(void) {
    static const uint8_t window[] = {0x21, 0, 127, 0x22, 0, 7};
    oled_send(false, window, sizeof window);
    oled_send(true, frame, sizeof frame);
}

static void dot(int x, int y) {
    if (x >= 0 && x < 128 && y >= 0 && y < 64) frame[(y / 8) * 128 + x] |= 1u << (y % 8);
}

// Glyph `g` with its top left at (x, y), each of its dots `scale` pixels square.
static void glyph(int x, int y, int g, int scale) {
    for (int col = 0; col < 5; col++)
        for (int row = 0; row < 7; row++)
            if (glyphs[g][col] >> row & 1)
                for (int i = 0; i < scale; i++)
                    for (int j = 0; j < scale; j++) dot(x + col * scale + i, y + row * scale + j);
}

// `text`, of digits, ':' and '-', from (x, y), a glyph every 6 * `scale` pixels.
static void text(int x, int y, const char *text, int scale) {
    for (; *text; text++, x += 6 * scale) glyph(x, y, *text == ':' ? 10 : *text == '-' ? 11 : *text - '0', scale);
}

// The DS3231's seconds to year registers; false where it does not answer.
static bool rtc_read(uint8_t *registers) {
    const uint8_t first = 0;
    if (i2c_write_blocking(i2c0, DS3231, &first, 1, true) != 1) return false;
    return i2c_read_blocking(i2c0, DS3231, registers, 7, false) == 7;
}

int main(void) {
    stdio_init_all();
    i2c_init(i2c0, 400000);
    gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_SDA);
    gpio_pull_up(PIN_SCL);
    oled_init();
    printf("ready\n");
    int shown = -1;
    for (;;) {
        uint8_t r[7];
        if (!rtc_read(r)) {
            printf("DS3231 absent\n");
            sleep_ms(1000);
            continue;
        }
        if (r[0] != shown) {
            shown = r[0];
            char time[9], date[11];
            snprintf(time, sizeof time, "%02x:%02x:%02x", r[2] & 0x3f, r[1], r[0]);
            snprintf(date, sizeof date, "20%02x-%02x-%02x", r[6], r[5] & 0x1f, r[4]);
            printf("time %s %s\n", time, date);
            memset(frame, 0, sizeof frame);
            for (int x = 0; x < 128; x++) dot(x, 0), dot(x, 63);
            for (int y = 0; y < 64; y++) dot(0, y), dot(127, y);
            text(16, 14, time, 2);
            text(34, 42, date, 1);
            oled_show();
        }
        sleep_ms(50);
    }
}
