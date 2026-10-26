# Touch UI with imui

This example runs an actual embedded UI inside the emulated MCU. The sysl
firmware is linked for flash, uses `imui`, renders antialiased vector graphics and TrueType text
through PlutoVG into ARGB32 RAM, converts to RGB565, and sends changed pixel runs to an ST7789VW over hardware SPI. Mouse input enters a
separate GT911 touch controller; firmware reads its ordinary I2C registers.
No host-side button callbacks change the guest application's state.

```sh
./examples/touch/build.sh
sysl build .
./benchtop run --window --board boards/touch.hocon --watch \
  --build ./examples/touch/build.sh \
  --watch-source examples/touch/main.sysl --watch-source examples/touch/runtime.sysl \
  examples/touch/demo.elf
```

- Click **ADD ONE** to increment a counter (0..9).
- Toggle **POWER** to drive the board's GPIO LED.
- Drag **BRIGHTNESS** to change the slider and progress bar.

The original workbench, audio and matrix examples remain separate boards.

## Graphics

PlutoVG now runs in the emulated MCU as well as rendering Benchtop's host window.
`imui` keeps its abstract `Painter` interface and supplies an optional
`sh.sysl.imui.plutovg.PlutoPainter` backend. This firmware enables that feature,
embeds the OFL-licensed ABeeZee TrueType font in flash, and rasterizes into a
240x240 ARGB32 buffer. Changed pixels are converted to RGB565 and sent over SPI.
The host does not draw guest text or substitute native widgets for the firmware.

The example uses Cortex-M33 cores with the single-precision FPU enabled, 520 KB
of MCU RAM, and a bounded 128 KB newlib heap. Globals occupy about 472 KB including
the heap, leaving the rest for the stack. `runtime.sysl` supplies the allocator's
`_sbrk` hook and explicitly rejects file/process operations. Building requires an
Arm GNU toolchain with newlib; `build.sh` locates its target headers and libraries.
The original small bitmap font remains in `font.sysl` for framebuffer examples,
but is not used by this renderer.

A full screen is initially transmitted, then only changed pixel runs. Each input
frame is recorded through imui's existing command recorder. Two fixed command
buffers identify damage in 30 horizontal bands; only damaged bands are replayed
through PlutoVG and compared with the previous RGB565 image. Unchanged frames
skip rasterization, pixel scanning and SPI entirely. Interaction runs once per
input, regardless of how many bands are redrawn.

With rounded widgets and hardware SPI, unchanged input drops from about 3.69
million guest ticks for full replay to 12 thousand with damage tracking. Button
press/release take about 1.43/1.75 million ticks (9.5/11.7 ms at the 150 MHz target),
and steady slider dragging takes about 939 thousand. These are emulator
instruction ticks, not hardware cycle timings. Matching command prefixes and
suffixes remain clean when focus adds an outline. The changed middle is still
compared by position. The firmware
exports `touch_profile` with rendering, flush and SPI ticks plus transmitted byte
and pixel counts. The performance test writes `target/touch-performance.txt`,
checks idle/drag budgets, and compares eight complete image hashes against a separately built
full-replay firmware image (`./examples/touch/build.sh full`).

The MCU now declares `clock-hz = 150000000`, and the desktop paces guest
execution at that target. Peripheral timebases inherit it. This is one tick per
emulated instruction, not a cycle-accurate Cortex-M33 model; no flash wait states
or bus contention are included. If the host cannot keep up, emulation runs slower
than the target. Stalls are capped at 50 ms of catch-up work per frame.
Headless runs and tests pump deterministic guest ticks without wall-time pacing.
The display now uses the catalog's `pl022` hardware SPI peripheral at
`0x50001000`, mode 0, 8-bit frames, divider 4: 37.5 MHz with the 150 MHz guest
clock. Firmware fills the eight-entry TX FIFO, polls TNF/BSY, and drains RX.
The controller emits timestamped SCK/MOSI transitions into the existing display
wire receiver, including CS/DC/reset handling. Peripheral time advances at MMIO
and GPIO accesses or word completion, rather than scheduling a host task per bit.
No host framebuffer shortcut or separate per-byte host thread is involved.

The demo selects ST7789VW, whose four-wire write timing allows this rate;
the older ST7789V catalog entry remains available. Both share the existing DCS
behavior model. See the [Sitronix ST7789VW datasheet](https://www.hestore.eu/en/prod_getfile.php?id=12938).
Button-press SPI work drops from about 1.26 million to 656 thousand guest ticks.
DMA request-control registers are present, but a DMA engine, SPI slave mode,
TI/Microwire framing and automatic hardware CS are not implemented yet.

The original GPIO implementation is preserved as a buildable feature:

```sh
./examples/touch/build.sh gpio
./benchtop run --window --board boards/touch.hocon examples/touch/gpio.elf
```

`./examples/touch/build.sh` builds the default hardware-SPI `demo.elf`. Both
transports have firmware regression coverage and identical reference pixels.

## Hardware description

The display and touch controller are independent catalog parts. The MCU routes
SDA/SCL to GP6/GP7; the ST7789 uses GP0..4 and the LED uses GP5. The host pointer
is bound to the display element rather than to a GPIO:

```hocon
input { pointer { display = touch } }
```

Pointer coordinates map only to the active glass, excluding the bezel, and
remain aligned with the native raster at different display densities. A drag captures the display and clamps at its edges;
release outside the display or focus loss releases the touch. Bounded report
buffering preserves quick taps across slow redraws.

The generic `gt911` model and physical `GT911` identity support either I2C
address 0x5d (default) or 0x14. Initial `width` and `height` default to 240. The
polling profile implements big-endian 16-bit register pointers, product ID,
8-byte single-contact reports, ready/ack status at 0x814e, and checksummed
configuration writes. It can share the virtual I2C controller with a DS3231.
INT/RESET wiring, multitouch, gestures and physical sensor scanning are not
implemented; omit INT and RESET in runnable descriptions.

The MCU's GPIO and I2C register adapters are virtual. The display/touch protocols
and `imui` code are hardware-style, but this image is not a physical Pico binary.

## Graphics dependencies

`imui` 0.2.0 adds the optional PlutoVG painter and fixes toggles/checkboxes to
paint their updated state in the activation frame. PlutoVG 0.2.6 makes its memory
rendering path usable without POSIX and checks caller-owned-surface allocation
failure. `imui` 0.2.1 improves damage matching and conservatively repaints when a changed
clip affects otherwise identical commands. The button-press rasterization/scan
cost drops from about 2.66 million to 724 thousand guest ticks. Both libraries
are developed in sibling worktrees. Validation runs inside
Benchtop: touch-driven UI behavior, actual display pixels, antialiased text,
repeated bounded-heap redraws, and flash-resident Thumb-2 execution.

Protocol references:
- [Goodix-authored GT911 programming guide](https://www.lcd-module.de/fileadmin/eng/pdf/zubehoer/GT911_Programming_Guide_Rev.10.pdf)
- [Linux Goodix driver](https://github.com/torvalds/linux/blob/master/drivers/input/touchscreen/goodix.c)

## Native display presentation

The glass is shown at 240×240 device pixels, including on Retina displays.
Guest RGB565 colors are copied to a native-pixel raster, without host rescaling
or smoothing. Window resizing changes the available viewport and leaves board
geometry at a fixed 1:1 layout scale. The bezel and other desktop controls remain
DPI-aware. The small syslUI 0.1.9 native-image operation makes this independent of
the desktop's display-density transform; mouse/touch mapping uses the same glass
rectangle.

Normal desktop execution uses persistent CPU workers, including a dedicated
core 0 worker. They pace themselves from the MCU clock, independently of UI
frames; the UI reads published CPU snapshots and locked display observations.
Pause, step, reset and reload wait for safe execution boundaries. Reset/reload
reuse the same host threads. `--deterministic` selects reproducible serial interleaving; headless
runs and firmware tests retain deterministic pumping.

## Rounded imui styling

The firmware enables `imui` 0.3.0 rounded widgets with `theme.radius = 8`.
PlutoVG performs the antialiased rasterization inside the MCU. The native-pixel
host view displays those RGB565 pixels directly. Changing the theme radius
changes the command record and therefore damages the appropriate display bands;
unchanged rounded shapes do not repaint. Square styling remains available by
setting radius to zero. Buttons, toggle track/knob and slider track/thumb are
rounded; the progress bar keeps its existing square design.

The full-reference firmware and GPIO-SPI firmware are built from the same source
using `full_redraw` and `gpio_spi` features. Tests compare full replay with damage
replay through eight interaction states, check antialiased corner pixels and
corner hit targets, and keep button redraws within a 60 Hz guest-frame budget.
