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
inputs and drives GP0 (heartbeat), GP1 and GP2. There is no host-generated LED animation.
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
