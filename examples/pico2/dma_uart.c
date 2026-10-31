// Written for benchtop: the DMA driving UART0 and paced by its own timers, each result printed for the
// test to compare with what the datasheet's rates make of it.
//
// 1. A string to UART0 by DMA, paced by UART0's TX request, its end taken as DMA_IRQ_1 while the core
//    sleeps in WFI: the interrupt comes once the last character is in the FIFO, 32 characters before
//    the line has sent it.
// 2. 200 words copied paced by pacing timer 0 at X/Y = 1/150: one a microsecond at the SDK's 150 MHz.
// 3. 16 words written through a write ring of 8 bytes: the last two words are what the ring holds.
// 4. A channel paced at 100 kHz (pacing timer 1, 1/1500) aborted after 205 us: about 20 transfers made.
// 5. Two channels chained: the second copies after the first, without the core.

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/uart.h"

static volatile int done;
static int tx_chan;

static void on_dma(void) {
    dma_irqn_acknowledge_channel(1, tx_chan);
    done++;
}

static const char text[] = "sent by the DMA, paced by UART0's TX request\r\n";
static uint32_t from[200];
static uint32_t to[200];
static uint32_t ring[2] __attribute__((aligned(8)));
static uint32_t chained[4];

int main() {
    stdio_init_all();
    printf("dma uart\n");

    // 1. UART0's TX by DMA, its end an interrupt.
    tx_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(tx_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, uart_get_dreq(uart0, true));
    dma_irqn_set_channel_enabled(1, tx_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_1, on_dma);
    irq_set_enabled(DMA_IRQ_1, true);
    uint32_t t0 = time_us_32();
    dma_channel_configure(tx_chan, &c, &uart_get_hw(uart0)->dr, text, sizeof text - 1, true);
    while (!done)
        __wfi();
    uint32_t t1 = time_us_32();
    uart_tx_wait_blocking(uart0);
    printf("irq %d after %u us\n", done, (unsigned) (t1 - t0));

    // 2. A pacing timer's rate.
    for (int i = 0; i < 200; i++)
        from[i] = 0x1000 + i;
    int timer = dma_claim_unused_timer(true);
    dma_timer_set_fraction(timer, 1, 150);
    int chan = dma_claim_unused_channel(true);
    c = dma_channel_get_default_config(chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, dma_get_timer_dreq(timer));
    t0 = time_us_32();
    dma_channel_configure(chan, &c, to, from, 200, true);
    dma_channel_wait_for_finish_blocking(chan);
    t1 = time_us_32();
    printf("200 words at 1/150 took %u us, last 0x%x\n", (unsigned) (t1 - t0), (unsigned) to[199]);

    // 3. A write ring.
    c = dma_channel_get_default_config(chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, 3);
    dma_channel_configure(chan, &c, ring, from, 16, true);
    dma_channel_wait_for_finish_blocking(chan);
    printf("ring 0x%x 0x%x, write address %s\n", (unsigned) ring[0], (unsigned) ring[1],
           dma_channel_hw_addr(chan)->write_addr == (uintptr_t) ring ? "back at the start" : "elsewhere");

    // 4. Abort mid-sequence.
    int slow = dma_claim_unused_timer(true);
    dma_timer_set_fraction(slow, 1, 1500);
    c = dma_channel_get_default_config(chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, dma_get_timer_dreq(slow));
    dma_channel_configure(chan, &c, to, from, 100, true);
    busy_wait_us(205);
    dma_channel_abort(chan);
    uint32_t left = dma_channel_hw_addr(chan)->transfer_count;
    printf("aborted with %u left, busy %d\n", (unsigned) left, dma_channel_is_busy(chan));

    // 5. A chain.
    int second = dma_claim_unused_channel(true);
    c = dma_channel_get_default_config(second);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    dma_channel_configure(second, &c, &chained[2], &from[10], 2, false);
    c = dma_channel_get_default_config(chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    channel_config_set_chain_to(&c, second);
    dma_channel_configure(chan, &c, chained, from, 2, true);
    dma_channel_wait_for_finish_blocking(chan);
    dma_channel_wait_for_finish_blocking(second);
    printf("chained 0x%x 0x%x 0x%x 0x%x\n", (unsigned) chained[0], (unsigned) chained[1], (unsigned) chained[2], (unsigned) chained[3]);
    printf("done\n");
}
