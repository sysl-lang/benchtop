// What an RP2350B program sees that an RP2350A program does not, on the SDK's own board header for the
// Pimoroni Pico Plus 2 (`pimoroni_pico_plus2_rp2350`, PICO_RP2350A 0): SYSINFO.PACKAGE_SEL, the 48 GPIOs, the
// ADC's eight GPIO inputs on GPIO 40 to 47 with the temperature sensor after them on input 8, and a
// toggling output on GPIO 40, which the QFN-60 package does not bond out.

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/structs/sysinfo.h"

#define HIGH_PIN 40
#define ADC_PIN 46

int main() {
    stdio_init_all();
    printf("package_sel %u, %u GPIOs, %u ADC inputs\n", (unsigned)sysinfo_hw->package_sel, (unsigned)NUM_BANK0_GPIOS, (unsigned)NUM_ADC_CHANNELS);

    adc_init();
    adc_gpio_init(ADC_PIN);
    adc_select_input(ADC_PIN - ADC_BASE_PIN);
    printf("GPIO %d is ADC input %d: 0x%03x\n", ADC_PIN, ADC_PIN - ADC_BASE_PIN, (unsigned)adc_read());

    adc_set_temp_sensor_enabled(true);
    adc_select_input(ADC_TEMPERATURE_CHANNEL_NUM);
    printf("temperature sensor is input %d: 0x%03x\n", ADC_TEMPERATURE_CHANNEL_NUM, (unsigned)adc_read());

    gpio_init(HIGH_PIN);
    gpio_set_dir(HIGH_PIN, GPIO_OUT);
    printf("toggling GPIO %d\n", HIGH_PIN);
    bool level = false;
    for (;;) {
        level = !level;
        gpio_put(HIGH_PIN, level);
        sleep_ms(1);
    }
}
