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

These are model boundaries for extending real-chip support, not a claim that PIO,
a complete pin mux, clock tree or DMA is already emulated. Host Kairos/libuv timers
serve build/watch/I/O workflows and do not stand in for guest hardware clocks.
