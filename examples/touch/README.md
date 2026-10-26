# Touch UI with imui

This example runs an actual embedded UI inside the emulated MCU. The sysl
firmware is linked for flash, uses `imui`, renders antialiased vector graphics and TrueType text
through PlutoVG into ARGB32 RAM, converts to RGB565, and sends changed pixel runs to an ST7789 over GPIO SPI. Mouse input enters a
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
of MCU RAM, and a bounded 128 KB newlib heap. Globals occupy about 468 KB including
the heap, leaving the rest for the stack. `runtime.sysl` supplies the allocator's
`_sbrk` hook and explicitly rejects file/process operations. Building requires an
Arm GNU toolchain with newlib; `build.sh` locates its target headers and libraries.
The original small bitmap font remains in `font.sysl` for framebuffer examples,
but is not used by this renderer.

A full screen is initially transmitted, then only changed pixel runs. GPIO
bit-banging and real guest rendering cost execution time. This is not a direct
host-rendered mockup; a virtual SPI controller and firmware DMA are future
performance improvements.

## Hardware description

The display and touch controller are independent catalog parts. The MCU routes
SDA/SCL to GP6/GP7; the ST7789 uses GP0..4 and the LED uses GP5. The host pointer
is bound to the display element rather than to a GPIO:

```hocon
input { pointer { display = touch } }
```

Pointer coordinates map only to the active glass, excluding the bezel, and
follow window scaling. A drag captures the display and clamps at its edges;
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
failure. Both are developed in sibling worktrees. Validation runs inside
Benchtop: touch-driven UI behavior, actual display pixels, antialiased text,
repeated bounded-heap redraws, and flash-resident Thumb-2 execution.

Protocol references:
- [Goodix-authored GT911 programming guide](https://www.lcd-module.de/fileadmin/eng/pdf/zubehoer/GT911_Programming_Guide_Rev.10.pdf)
- [Linux Goodix driver](https://github.com/torvalds/linux/blob/master/drivers/input/touchscreen/goodix.c)
