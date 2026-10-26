# GPIO LED matrix demo

A reusable `led-matrix-5x5` catalog part models 25 LEDs driven by five active-high
row anodes and five active-low column cathodes. `microbit-v2-led-matrix` selects
the micro:bit V2 display assembly using that same model. Row/column terminals
are numbered 0..4 in visual order. V1's 3x9 electrical wiring is not modeled.

The model reuses the seven-segment GPIO scan/persistence engine. Brightness is
linear in each LED's actual guest-cycle on-time, including output enable,
blanking, scanning, and software PWM. The renderer compensates the normal 1/5
row scan; it never changes firmware-visible state. Averaging covers one to two
`persistence-cycles` windows. Pausing guest execution freezes the display.
Light sensing via LED junction discharge is not modeled.

`boards/matrix.hocon` is a virtual Cortex-M0+ prototype, not an emulated Nordic
micro:bit MCU. The display accepts the same GPIO waveforms as a micro:bit V2,
but this demo's GPIO register addresses belong to Benchtop's virtual MCU.
Running unmodified complete micro:bit firmware also needs its Nordic MCU model.

The sysl firmware blanks rows before changing columns, scans a heart and a
diagonal, and cycles through 25%, 50%, 75%, and 100% PWM brightness.

From the repository root:

```sh
./examples/matrix/build.sh
sysl build .
./benchtop run --window --board boards/matrix.hocon --watch \
  --build ./examples/matrix/build.sh --watch-source examples/matrix/main.sysl \
  examples/matrix/demo.elf
```

Wiring reference: https://tech.microbit.org/hardware/schematic/
