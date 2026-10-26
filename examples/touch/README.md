# Touch UI with imui

This example runs an actual embedded UI inside the emulated MCU. The sysl
firmware is linked for flash, uses `imui`, draws RGB565 pixels into RAM, and
sends changed pixel runs to an ST7789 over GPIO SPI. Mouse input enters a
separate GT911 touch controller; firmware reads its ordinary I2C registers.
No host-side button callbacks change the guest application's state.

```sh
./examples/touch/build.sh
sysl build .
./benchtop run --window --board boards/touch.hocon --watch \
  --build ./examples/touch/build.sh \
  --watch-source examples/touch/main.sysl --watch-source examples/touch/font.sysl \
  examples/touch/demo.elf
```

- Click **ADD ONE** to increment a counter (0..9).
- Toggle **POWER** to drive the board's GPIO LED.
- Drag **BRIGHTNESS** to change the slider and progress bar.

The original workbench, audio and matrix examples remain separate boards.

## Graphics

PlutoVG renders Benchtop's host window, not the MCU's framebuffer. `imui`
provides layout, widget interaction and drawing commands through its `Painter`
trait. This example chooses its allocation-free `FrameBuffer` painter, which
uses RGB565 rectangle fills and one-bit bitmap text. The original 3x5 font in
`font.sysl` is doubled to 6x10 pixels. It deliberately has a pixel appearance;
better fonts or another embedded painter can improve the visual quality.
The emulated ST7789 displays whatever pixels firmware sends.

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

## imui fix

The dependency pins commit `6ee7c464ce50ad7d3f155a32b3071a01a0823b70` from the
`benchtop-touch` branch. It makes toggles and checkboxes paint their new state
in the activation frame instead of requiring an extra frame. The change was
made in the sibling `imui-benchtop-touch` worktree; the original imui checkout
was not modified.

Protocol references:
- [Goodix-authored GT911 programming guide](https://www.lcd-module.de/fileadmin/eng/pdf/zubehoer/GT911_Programming_Guide_Rev.10.pdf)
- [Linux Goodix driver](https://github.com/torvalds/linux/blob/master/drivers/input/touchscreen/goodix.c)
