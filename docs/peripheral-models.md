# Peripheral and component boundaries

Chip peripherals expose the registers firmware reads and writes. External components
consume or supply signals on named connection points. Their behavior is independent
of the chip register map: the seven-segment model takes resolved digital levels,
output enables and guest time, while the potentiometer supplies integer microvolts.
The virtual workbench publishes GPIO transitions to its bound display
models. Conversion completion and timer alarms use the board's shared guest clock
and scheduler, and raise the configured interrupt inputs.

GPIO bases already come from each peripheral's HOCON `at`. A reusable GPIO engine
can also accept register layouts: offsets, widths, reset values and operations such
as output, direction, set bits and clear bits. Address parameters alone do not change
register semantics. A real MCU model must choose the matching operations and bit
layout, with named profiles reusable across chips. The `panel-gpio` model has a
fixed register layout; configurable layouts are not implemented.

For PIO, PWM, alternate functions and GPIO to share pads, a chip model needs a pin
routing/multiplexer layer. Each producer supplies output values and output enables;
the mux selects which source reaches each pad, input paths sample that pad, and
external parts observe the resulting transitions. Such routing must precede display
observation rather than giving every peripheral its own unrelated copy of the pins.
PIO also needs the chip's clock domains, interrupts and DMA requests. The RP2350's
PIO connects to GPIO, bus fabric and interrupts and has clock dividers; timer
registers remain separate peripherals. See the [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf).

The workbench now has a minimal static PWM pad override and timestamped audio
integration. Its PWM and byte-level I²C register adapters are explicitly virtual
and reusable; the DS3231 is a separate slave model behind I²C device callbacks.
See [audio and RTC](audio-and-rtc.md) for the implemented boundaries.

## Unsupported writes are refused, not ignored

Every chip peripheral model follows one policy for a write it cannot honour — a reserved
bit, a mode the model does not implement, a configuration change the silicon would
accept but the model cannot carry out: the model's `write` answers `false`, and the bus
adapter turns that into a fault the guest (and the person running it) sees at the
offending store. Real silicon would accept such a write and then behave in a way the
model cannot reproduce, so a silent acceptance would let firmware run on into behaviour
that is wrong without any sign. A refusal therefore marks the edge of a model, never a
hardware error the firmware should expect.

Each model's header lists its own divergences. The current ones:

| model | divergence |
|---|---|
| `peripheral/sdmmc` | refuses reserved bits and reconfiguration during a transfer; starts the data path only after an error-free R1, where the real DPSM does not look at the response |
| `peripheral/dma` | refuses FIFO/burst/unequal-width/memory-to-memory streams; ignores writes to the read-only LISR/HISR, as the silicon does |
| `peripheral/stm32_spi` | refuses CRC, DMA, hardware NSS, finite TSIZE and non-8-bit frames |
| `peripheral/dsi` | refuses video mode, more than two lanes and out-of-range PLL settings |
| `parts/wm8994` | NACKs an unmodelled register address and configurations outside the 48 kHz slave subset |
| `parts/ds3231` | none: it keeps the chip's own leap-year rule (2100 is a leap year), not the Gregorian one |

These are model boundaries for extending real-chip support, not a claim that PIO,
a complete pin mux, clock tree or DMA is already emulated. Host Kairos/libuv timers
serve build/watch/I/O workflows and do not stand in for guest hardware clocks.
