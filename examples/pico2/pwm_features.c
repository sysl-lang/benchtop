// What pico-examples' PWM programs do not reach, through the SDK: a DMA channel paced by a slice's wrap,
// a GPIO edge interrupt on a pin PWM drives, and a slice counting rising edges of its B pin. The test wires
// GPIO 4 (slice 2, channel A) to GPIO 11 (slice 5, channel B).

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

static const uint16_t levels[4] = {10, 20, 30, 40};
static volatile uint32_t rising;

static void on_edge(uint gpio, uint32_t events) {
    if (gpio == 4 && (events & GPIO_IRQ_EDGE_RISE)) rising++;
}

int main() {
    stdio_init_all();

    // Each wrap of slice 3 asks for one transfer into channel A's half of CC.
    uint slice = 3;
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_wrap(&cfg, 999);
    pwm_config_set_clkdiv_int(&cfg, 10);
    pwm_init(slice, &cfg, true);
    int ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pwm_get_dreq(slice));
    dma_channel_configure(ch, &c, &pwm_hw->slice[slice].cc, levels, 4, true);
    dma_channel_wait_for_finish_blocking(ch);
    sleep_ms(1);
    printf("dma: cc %u\n", (unsigned) (pwm_hw->slice[slice].cc & 0xFFFF));
    pwm_set_enabled(slice, false);

    // Slice 5 counts the rising edges slice 2 makes, which the interrupt counts too.
    pwm_config counter = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&counter, PWM_DIV_B_RISING);
    pwm_init(5, &counter, false);
    gpio_set_function(11, GPIO_FUNC_PWM);
    pwm_set_enabled(5, true);

    gpio_set_irq_enabled_with_callback(4, GPIO_IRQ_EDGE_RISE, true, on_edge);
    pwm_config out = pwm_get_default_config();
    pwm_config_set_wrap(&out, 999);
    pwm_config_set_clkdiv_int(&out, 100);
    pwm_init(2, &out, false);
    pwm_set_chan_level(2, PWM_CHAN_A, 500);
    gpio_set_function(4, GPIO_FUNC_PWM);
    pwm_set_enabled(2, true);
    while (rising < 10) tight_loop_contents();
    pwm_set_enabled(2, false);
    pwm_set_enabled(5, false);
    gpio_set_irq_enabled(4, GPIO_IRQ_EDGE_RISE, false);
    printf("edges: interrupt %u counter %u\n", (unsigned) rising, (unsigned) pwm_get_counter(5));
    return 0;
}
