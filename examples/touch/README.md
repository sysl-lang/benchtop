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

## Discovery audio and touchscreen

Build with `./examples/touch/build.sh audio`, then run:

```sh
./benchtop run --window --board stm32h747-disco examples/touch/audio-disco.elf
```

M4 runs the native-pixel DSI UI. Enable TONE to hear M7's 440 Hz sine through
SAI/DMA and the WM8994 speaker path; CHANGE TONE selects higher pitches and
VOLUME controls amplitude. Startup is silent. The guest uses its FPU to
interpolate samples and DMA half/full IRQs to refill a circular buffer. Exported
`audio_errors`, `audio_irqs` and `audio_deadline_misses` help inspect the workload.
See [supported audio hardware](../../docs/stm32-audio-peripherals.md) for limitations.

## Discovery microphone effects

```sh
./examples/touch/build.sh mic
./benchtop run --window --board stm32h747-disco examples/touch/mic-disco.elf
```

The input meter shows the Mac microphone as measured by M7 after SAI capture DMA.
MONITOR starts off; enable it for playback, CHANGE EFFECT for dry/low-pass/high-pass,
and VOLUME for gain. M7 processes the completed receive half into the free transmit
half on DMA IRQ60, while M4 draws the meter and handles touch. The generated sine
example is retained as `audio-disco.elf` (`build.sh audio`).

## Stereo headphone delay

```sh
./examples/touch/build.sh delay
./benchtop run --window --board stm32h747-disco examples/touch/delay-disco.elf
```

M7 runs separate 100/150 ms delay lines in D2 SRAM1 and sends DAC1 stereo PCM to
the headphone part through SAI/DMA. M4 provides MONITOR, FEEDBACK, WET/DRY,
input/output meters and estimated DSP peak load. Monitoring starts off. The
headphone part uses the Mac's default playback device; the speaker, sine and
microphone-filter demos remain available.

## SD-card settings

The `storage` variant uses the Discovery board’s native SDMMC1 slot, keeps the stereo delay and adds **SAVE**. It writes
`SETTINGS.TXT` (feedback and wet/dry percentages) and restores it at startup through the standalone
[`sd`](https://github.com/sysl-lang/sd)
and [`fat`](https://github.com/sysl-lang/fat) firmware dependencies.

```sh
sysl run tools/mk-sd-image
./examples/touch/build.sh storage
./benchtop run --window --board boards/stm32h747-sd.hocon examples/touch/storage-disco.elf
```

Run these commands from the Benchtop root. The image tool creates a 16 MiB FAT16
volume at `target/disco-sd.img`; it preserves any existing file. The board's
`image` and `sectors` parameters must agree exactly. Guest reset preserves the
card's storage. Restarting Benchtop reloads the disk image. An omitted/empty
`image` gives volatile zero-filled media instead. Firmware never formats media
on initialization failure: the UI reports SD ERROR.

The default connection is the Discovery microSD slot CN12: SDMMC1 at
`0x52007000`, IRQ49, PC8..PC11 D0..D3, PC12 CK and PD2 CMD (AF12), plus
active-low card detect on PI8. M4 drives actual GPIO and SDMMC registers and
uses SDMMC's single-buffer internal DMA through a word-aligned AXI SRAM buffer.
The native driver in `sd` performs CMD0/8, ACMD41, CID/RCA/CSD, selection, SCR and
four-bit negotiation. The controller has a configured 60 MHz kernel clock;
firmware selects 400 kHz initialization and 15 MHz four-bit transfers under its
25 MHz transfer ceiling. Full physical-board RCC/PLL startup remains separate.

The register model supports native SD v2 memory cards, one 512-byte sector per
transfer, the eight-byte SCR transfer, single-buffer IDMA, completion/error IRQs
and programming-busy polling. PIO FIFO, multiblock, double-buffer IDMA, SDIO,
MMC/eMMC, UHS and busy-end IRQ are not implemented. Unsupported register modes
are rejected. Command/data timing follows configured bus clocks; RAM block
visibility occurs at transfer completion, without detailed AHB arbitration.
The reduced 16 MiB SDHC-addressed image is for development, rather than a real
SDHC card's capacity class.

The complete SPI variant remains available:

```sh
./examples/touch/build.sh storage-spi
./benchtop run --window --board boards/stm32h747-spi-sd.hocon examples/touch/storage-spi-disco.elf
```

It uses the external slot on PA5 SCK, PA6 MISO, PA7 MOSI and PA4 CS (SPI1 AF5),
eight-bit polling, software chip select and continuous TSIZE=0 transfers, at
234.375 kHz initialization and 15 MHz transfers. The same FAT, settings, WAV,
recording/playback, media cache and disk writer are shared between transports.

A separate host worker writes accepted sectors to disk; the guest core does no
host filesystem I/O. Card busy stays asserted until that write has been flushed
to the OS. SAVE reports success only after the firmware's FAT/SD flush. This
is development storage, without power-cut simulation or a host-power-loss
persistence guarantee. The model has one cached card (up to 64 MiB) per board.

Startup accepts the exact settings format written by SAVE, with feedback
0–85 and wet/dry 0–100. LOADED confirms successful restoration. A missing file
keeps the 35/35 defaults and shows SD READY. Invalid contents keep both defaults
and show BAD FILE; SAVE can replace that file. Storage read/mount failures
show SD ERROR. Monitor always starts off, including after settings restoration.

## WAV recording

The `storage` demo also has RECORD / STOP. It records the stereo microphone DMA
input at 48 kHz, PCM16, into `RECORD.WAV` on the SD image, using the standalone
[`wav`](https://github.com/sysl-lang/wav) dependency. Monitoring is independent:
recording works with MONITOR off. Each RECORD replaces the previous RECORD.WAV;
STOP drains pending audio, patches WAV lengths and flushes the FAT/SD storage.
Wait for REC SAVED before closing/resetting. An interrupted recording may have an
unfinished header. PLAY can replay the finalized recording.

M7 remains the audio producer. Its DMA IRQ publishes blocks into a 64 KiB SPSC
ring in D2 SRAM (about 341 ms at 192,000 bytes/second). M4 copies at most 4 KiB
per loop into its own buffer, releases ring slots, and performs the filesystem
and SPI work. A generation acknowledgment stops capture before finalization, so
no final DMA half is silently lost. SAVE is ignored while recording/finishing
because the filesystem cache has one owner.

A full ring stops capture without blocking M7; the buffered prefix is finalized
and the UI shows OVERRUN. A storage failure shows REC ERROR and makes no promise
that the failed file is playable. The exported record_frames and record_overruns
counters are available to debugger instruments. The existing firmware startup
keeps caches disabled; a physical application enabling the M7 data cache must
configure shared SRAM appropriately or perform cache maintenance.

The SPI variant uses FIFO-pipelined bulk SPI reads/writes rather than waiting for each
byte separately. At most 32 bytes are outstanding, and writes drain RX too.
The transfer-clock ceiling is 25 MHz (15 MHz with this board's 60 MHz kernel
clock and available dividers), within standard-speed SD operation. Initialization
still stays below 400 kHz. The ring capacity remains 64 KiB.

## WAV playback

PLAY reads `RECORD.WAV` through FAT and the shared `wav` reader. The initial
player accepts 48 kHz, stereo PCM16, matching recordings made by this demo;
other rates/encodings report WAV FORMAT without starting playback. MONITOR can
stay off. During playback the file replaces the live delay output; live DSP
continues running and resumes at Stop or EOF.

M4 prefills the existing 64 KiB audio ring and continues reading 4 KiB chunks;
M7 consumes it into the normal SAI/DMA output. Recording and playback take turns
using that ring and the FAT cache. SAVE/REC are ignored during playback, and
PLAY is ignored while recording or finalizing. No extra audio buffer is added.
The bottom controls are SAVE, REC/STOP and PLAY/STOP.

PLAY DONE means EOF or manual Stop. NO WAV, WAV FORMAT, PLAY ERROR and UNDERRUN
report missing files, unsupported/malformed input, I/O failure and starvation.
On an underrun the missing frames are silenced and playback stops without
blocking M7. An immutable total frame count distinguishes EOF from starvation,
including files smaller than one DMA half or exactly one ring buffer long.
`play_tail`, `play_total` and `play_underruns` are exported for instruments.
EOF and Stop use a generation acknowledgment before the shared ring is reused.
EOF/Stop does not alter the WAV or card contents. Rate conversion, compressed
formats and simultaneous record/playback are future extensions.
