// pico-examples' adc/dma_capture (dma_capture.c, unchanged beside this file) with core 1 left unlaunched.
// Its core 1 drives a resistor DAC from PIO0 on GPIO 6 to 10, and the PIO's pins do not reach the GPIO mux
// yet (docs/rp-coverage.md item 14), so `pio_gpio_init` is refused by name. The ADC, its FIFO and the DMA
// are all as the example has them, and the test puts a level on GPIO 26 itself.

#include "pico/multicore.h"
#define multicore_launch_core1(entry) ((void)(entry))
#include "dma_capture.c"
