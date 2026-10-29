# Instruments

An instrument observes the emulation without becoming part of the board or product.
The first instrument is a scrolling plot. A part association adds a small plot icon
for opening the instrument; it can also be opened from the toolbar in a
movable, resizable native window. Closing the window does not stop recording
or affect firmware.

The demo automatically loads `instruments/workbench.hocon`. Use an explicit set for
another session:

```sh
./benchtop run --window --board workbench \
  --instruments instruments/workbench.hocon examples/visual/demo.elf
```

```hocon
instruments {
  adc {
    instrument = plot
    title = "ADC result"
    source = controller.adc.RESULT
    part = volume
    units = counts
    min = 0
    max = 4095
    capacity = 1024
    window-cycles = 20000000
    sample-cycles = 20000
  }
}
```

`part` is optional and names an existing part instance, rather than its `ref`.
Leave it out for a standalone instrument. Each instrument gets a toolbar action.
Unknown fields, sources, parts, types, duplicate fields and invalid bounds are
rejected before opening the application window.

## Numeric sources

The plot model accepts a numeric callback returning a value or a missing sample;
it has no knowledge of ADCs, parts or CPU architectures. The windowed backend
binds these read-only names. The ADC register names come from the ADC model, and the
`cpu` names are the Arm core's `r0` through `r15`; a RISC-V core's registers (`x0`
through `x31`) have no names here, and a board with a different number of cores gets
none beyond the two:

| Source | Observation |
| --- | --- |
| `<chip>.<adc>.RESULT` | Most recently completed virtual ADC conversion |
| `<chip>.<adc>.CTRL`, `CHANNEL`, `STATUS` | ADC control, selected channel and status |
| `<chip>.<adc>.IRQ_ENABLE`, `IRQ_STATUS` | ADC interrupt mask and pending bits |
| `<chip>.<adc>.VREF`, `RESOLUTION`, `CONVERSION_CYCLES` | ADC parameters |
| `<chip>.<gpio>.IN`, `OUT`, `OE` | Resolved pin levels, raw output latch and output-enable mask |
| `cpu0.pc`, `cpu0.retired`, `cpu0.r0` through `cpu0.r15` | Core 0 state |
| `cpu1.pc`, `cpu1.retired`, `cpu1.r0` through `cpu1.r15` | Core 1 state |
| `ram.u8.<offset>`, `ram.u16.<offset>`, `ram.u32.<offset>` | Little-endian backing RAM at a decimal byte offset |

Chip and peripheral names come from the description: `controller.adc.RESULT` is
an example, not a reserved ADC name. RAM offsets refer to the emulated backing
array, not arbitrary bus addresses; bounds are checked during binding. The source
binder supports single-board scenes.

Not modelled: product-wide execution, and observation sources from peripherals other
than the ADC and the panel GPIO.

Observations happen between deterministic guest execution slices. ADC observation
does not advance conversion time, acknowledge status, or change interrupts. RAM
observation never invokes an MMIO handler. Sampling is a frame-boundary snapshot,
not a trace of every register write; short transitions between snapshots can be
missed.

## Time and history

The horizontal axis is guest cycles, with the most recent sample at the right.
Paused firmware does not add repeated samples or scroll the plot. Reset and
firmware reload clear history. A guest-clock rewind also clears it. Capacity is
bounded (2–65536 samples); the oldest samples are overwritten in chronological
order. The default capacity, interval and time window are shown above.

Hold freezes recording independently of CPU pause. Resuming marks a gap rather
than drawing a transition through time that was not observed. Missing and
nonfinite source values also create gaps. The vertical range is fixed by `min`
and `max`; the trace is clipped to it, while the latest-value label reports the
actual observed value.

In the instrument window, Hold/Resume and Clear are available as buttons.
Space toggles Hold, C clears history, and Escape closes that instrument window.
These keys do not reach board controls. Main-window shortcuts retain their normal
meaning. PNG exports capture the main workbench; separate instrument windows are
not composited into that image.
