# Firmware-driven visual workbench

From the repository root, build benchtop with sysl 0.6.2 and installed SDL3:

```sh
sysl build . -o benchtop
./benchtop demo
```

The firmware is written in sysl (`main.sysl`) and targets `thumbv6m-freestanding`.
The committed demo ELF is embedded, so running the demo needs no cross compiler.
To change the firmware, install GNU Arm binutils (`arm-none-eabi-ld`), then:

```sh
cd examples/visual
./build.sh
cd ../..
./benchtop run --window --board workbench examples/visual/demo.elf
```

`sysl build-c` emits an archive directly from sysl; despite its name, this does not
produce or compile C. GNU Arm ld links it with `demo.ld`. The reset vector table is
a static sysl struct placed in `.rodata.vectors`, containing a linker-symbol
address and a reset function pointer. Compile-time assertions check its size and
reset-vector offset. No C or assembly source is needed. Unused compiler runtime helpers are
discarded by `--gc-sections`; the firmware needs no libc or heap.

Rebuild benchtop to update its embedded demo. Hold 1/2 or the onscreen buttons;
Space pauses, N steps, R resets, S saves a PNG. Firmware reads GP8/GP9 as active-low
inputs and drives GP0 (heartbeat), GP1 and GP2. Turn RV1 on the board or use its
inspector slider: its wiper drives GP26, connected to ADC channel 0. Firmware
starts conversions, polls their completion, converts the result to decimal digits,
and multiplexes DS1 using GPIO writes. At 0%, 50% and 100%, DS1 shows `0000`, `2048`
and `4095` respectively. The firmware produces no console output.
The firmware console is at 0x40000000. The workbench board is a virtual Cortex-M0+
platform, not an RP2350 memory map or Pico SDK target.

`panel-gpio` is a 256-byte MMIO peripheral at 0x50000000. Accesses must be aligned
32-bit words. OUT (0x00) stores output levels; IN (0x04, read only) combines enabled
outputs with external inputs; OE (0x08) enables output drivers; EDGES (0x0c) latches
input transitions and is write-one-to-clear; IRQ_MASK (0x10) selects transitions;
IRQ_STATUS (0x14, read only) is EDGES & IRQ_MASK. IRQ2 is asserted while status is
nonzero. Unknown registers are unmapped; unsupported access sizes fault.

Headless smoke test and image export:

```sh
./benchtop demo --headless --frames 60 --screenshot /tmp/workbench.png
./benchtop render pico-breadboard-kit /tmp/kit.png
sysl test . --jobs 1
```

The desktop uses syslUI widgets and its shared SDL3 application driver. The board
is a custom syslUI view rendered through PlutoVG, including antialiased shapes and
font rendering. Headless exports render the same view tree to a 1200×760 PlutoVG
surface without initializing SDL. A system font is required in both modes.
The window handles display density and resizing through the toolkit driver;
the inspector scrolls when its contents exceed the available space. Console
capture retains a bounded recent tail for long-running firmware.

Build and reload automatically while editing sysl firmware:

```sh
./benchtop run --window --board workbench --watch \
  --build ./examples/visual/build.sh \
  --watch-source examples/visual/main.sysl \
  --watch-source examples/visual/demo.ld \
  examples/visual/demo.elf
```

Run this from the repository root with libuv installed. Kairos runs the child build,
reads stdout and stderr asynchronously, and watches for file changes. Use the Build
button or B to rebuild, and the Build console tab for diagnostics. A successful
changed image resets the guest; a failed build leaves its previous firmware running.

## Virtual ADC

`virt-adc` is an explicit virtual chip peripheral at 0x50000100, with IRQ3 and
`channels = [GP26]`. It uses a 3,300,000 μV reference and 32 guest cycles per conversion.
Both are configurable as `reference-uv` and `conversion-cycles`. Resolution is 12 bits.
Samples round to the nearest code (half steps up), saturating at 0 and 4095.
Unconnected channels report an error instead of supplying a fabricated voltage.

| Offset | Register | Behavior |
|---|---|---|
| 0x00 | CTRL | Bit 0 enables; writing bit 1 starts a conversion. Disabling aborts a pending conversion. |
| 0x04 | CHANNEL | Zero-based index into the peripheral's declared channels. |
| 0x08 | STATUS | Bit 0 busy, bit 1 ready, bit 2 error. Ready/error are write-one-to-clear. |
| 0x0c | RESULT | Read-only, holds the last completed sample. |
| 0x10 | IRQ_ENABLE | Bit 0 completion, bit 1 error. |
| 0x14 | IRQ_STATUS | Sticky completion/error bits, write-one-to-clear. Masked pending bits assert IRQ3. |
| 0x18 | REFERENCE_UV | Read-only reference voltage. |
| 0x1c | RESOLUTION | Read-only, 12 bits. |
| 0x20 | CONVERSION_CYCLES | Read-only configured duration. |

Only aligned 32-bit accesses are supported. Other access widths fault and unknown
registers are unmapped. Starts while disabled, busy, or on an unconnected channel
set the error flag; a busy restart preserves the sample already in flight. Inputs
changed during a conversion affect the next sample. A guest WFI can wake on the
completion interrupt. Reset clears ADC registers while the external knob retains
its position. No analogue circuit solver, noise or source-impedance model is used.

The component declaration uses `ref = RV1` and `wiper = controller.GP26` directly.
Its ideal voltage range and initial position come from its HOCON instance parameters.

## Seven-segment display

The built-in `seven-segment` part has `digits` (1–8, default 1), `common` (`cathode`
or `anode`, default `cathode`), `color` and `persistence-cycles` (default 50000).
All `a`–`g` terminals and `digit0` through `digitN` must be connected. `dp` is optional.
Each terminal uses a named chip connection, for example `a = controller.GP10`.

DS1 uses four common-cathode digits. Segment bits a,b,c,d,e,f,g,dp map to GP10–17;
left-to-right digit commons map to GP18–21. Firmware makes these pins outputs,
sets every common high to blank the display, changes segment lines, then selects
one common low. It repeats this scan while servicing the ADC and buttons.
The decimal point is wired but unused by this firmware.

The component model accepts no number or text. It observes physical GPIO patterns
and output enables, supports static and multiplexed drive, and uses guest-cycle
visual persistence to show scanned digits. A continuously selected digit stays lit
without further writes; deselected digits fade after the configured persistence.
Input/high-impedance pins cannot light a segment, and common-anode wiring reverses
both segment and common polarity. Reset clears the display's retained scan state.
This models software-visible pin behavior with ideal electrical drive; brightness,
current limits and ghosting intensity are not circuit-simulated.

The scan algorithm and segment encodings are suitable for equivalent real wiring.
The current workbench GPIO/ADC register map is virtual; a binary for a real MCU
still needs that MCU's register interface and startup code.
