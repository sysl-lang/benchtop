# RP2040 peripheral models

Reusable, instance-owned models for RESETS, TIMER, the watchdog's tick generator, the SIO
integer divider and the SIO interpolators (the last shared with the RP2350's SIO). They import the SVD-generated `github.com/sysl-lang/rp2040` register constants. No
model imports a CPU module, uses global device state, starts a host thread, or reads the host clock.

RESETS, TIMER and the watchdog are library peripherals (`rp-resets`, `rp-timer`, `rp-watchdog`),
placed by `mcus/rp2040.hocon` and run by `sh.sysl.benchtop.virt` (`virt/rp.sysl`) on any runnable
board whose mcu places them. The `rp2040` mcu itself runs on `chip.Rp2040` (`chip/rp2040.sysl`), which
places these models beside the RP2350's shared ones (SIO with this divider and interpolator, GPIO and
pads, UART, the clock generators, PSM) at the RP2040's addresses. DMA, SPI, I2C, PWM, PIO, USB, the SSI
and flash, and the real boot ROM are separate work (docs/rp-coverage.md §7).

## Machine integration contract

Construct with `resets()`, `timer()`, `watchdog()` and `divider()`, or explicitly call `reset()` on
fresh storage. All register methods take a relative offset, a transfer width in bytes (1, 2 or 4),
and, for writes, the value. Reads return `Result[u32, AccessError]`; writes return
`Result[unit, AccessError]`. The machine maps addresses, routes errors to its fault/diagnostic policy,
and serializes shared peripheral access across CPU threads. Reset and clock delivery must use the
same serialization. There are no internal locks and copying a struct copies its complete device
state.

- **RESETS:** one shared `Resets` instance. `held` is the 25-bit reset-output mask; `watchdog` is
  WDSEL. After a RESET write, the machine applies asserted resets to connected devices, releases the
  others and calls `complete(bits)` when those devices are ready. RESET_DONE stays low until that
  handshake; writing a reset bit clears its done bit. benchtop's board completes every released
  device at once.
- **Watchdog:** one shared `Watchdog`: TICK (CYCLES, ENABLE, RUNNING, COUNT) and SCRATCH0–7. The
  counter's registers (CTRL, LOAD, REASON) are refused as unmapped. `advance(cycles)` takes clk_ref
  cycles and answers the ticks made; `cycles_to(n)` answers the cycles until the nth tick, `None`
  while stopped. It powers up enabled with CYCLES 0, so stopped; a TICK write restarts COUNT from
  CYCLES.
- **TIMER:** one shared `Timer`. Supply the watchdog's ticks with `advance(ticks, halted)`, where
  `halted` bit 0/1 indicates the debug-halted state of each processor. No tick delivery means no
  counter progress. The machine must split clock advancement at writes or debug-state changes.
  `irqs()` is a four-bit level output, normally routed to RP2040 IRQs 0–3 on both processors.
  `next_alarm(halted)` returns an optional distance in input ticks, allowing a scheduler to advance
  while the CPUs sleep. It does not claim that the tick source is running, or that an interrupt is
  enabled in either CPU.
- **Divider:** two `Divider` instances, one per processor. Dispatch the SIO offsets 0x60–0x78 to
  the requesting processor's instance, alongside the shared SIO implementation. Supply that
  divider's input clocks with `advance(cycles)`. `remaining` is the distance to completion, 0
  when ready. Every operand write restarts eight clocks; result writes cancel pending work.
  Read the remainder before the quotient when preserving DIRTY for SDK context-save logic.
- **Interpolators:** four `Interp` instances, `interp(n, rotate)` for each processor's INTERPn.
  Dispatch the SIO offsets 0x80–0xBC (INTERP0) and 0xC0–0xFC (INTERP1) of the requesting
  processor, relative to the interpolator and word-aligned; take a byte or halfword read's lane
  of the word. `rotate` is false on the RP2040 (SHIFT is a logical shift) and true on the RP2350
  (a rotate); `peripheral/rp2350/sio.sysl` places them so.

The peripheral methods do not assert CPU pending bits themselves. In particular, the machine must
resample level IRQs at scheduling/exception boundaries as well as after register writes.
`virt/rp_tests.sysl` runs guests against RESETS, TIMER and the watchdog on a board, one thread and
two; `v6m/rp2040_peripheral_tests.sysl` supplies a test-only bus for the dividers.

## Register behavior and boundaries

TIMER implements staged time writes, the shared high-word read latch, raw reads, four one-shot
comparators, cancellation, raw/enable/force/status interrupt registers, software pause and debug
pause. Advancing over multiple low-word wraps still finds armed alarms. A time-register write
compares only the newly written value; it is not elapsed time. Alarms programmed equal to the
counter match immediately. Each fired alarm disarms and latches its raw interrupt independently
of the interrupt-enable mask.

RESETS, TIMER and the watchdog implement RP2040 replicated byte/halfword writes, little-endian
narrow reads, and the +0x1000/+0x2000/+0x3000 XOR/SET/CLR aliases for ordinary RW registers. Reads
through these aliases have the same effects as a base read. A write-1-to-clear flag (TIMER's
ARMED and INTR) clears on a 1 in the data through the plain address and every alias, and a
self-clearing control (the watchdog's CTRL.TRIGGER) fires on a 1 through any alias but CLR — one
rule for every model on both chips, stated with its evidence in `access.sysl` (`w1c`, `triggered`).
To avoid inventing unverified hardware behavior, atomic-alias writes to TIMER's write-only time
registers return `UnsupportedAlias`; normal writes to them are implemented. Read-only
writes are ignored; write-only time reads return zero. Unknown registers, invalid widths and
unaligned transfers return explicit errors without changing state. SIO has no bus atomic aliases.

Divider results during a busy calculation are architecturally unspecified; this model retains
the old results until completion. Signed division uses truncation toward zero and the dividend's
remainder sign. INT_MIN/-1 wraps to INT_MIN. Zero-divisor behavior follows the Pico SDK's host
implementation: unsigned quotient -1; signed quotient minus the sign of the dividend (including
zero for 0/0); remainder equal to the dividend. Physical-board differential validation of these
edge behaviors, comparator synchronization latency and bus-cycle timing remains outstanding.

## Sources and validation

- [RP2040 datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf): sections
  2.1.2–2.1.4 (bus aliases/narrow accesses), 2.3.1.5 (divider), 2.14 (resets), 4.6 (timer),
  4.7 (watchdog, its tick generator).
- [Pico SDK host divider](https://github.com/raspberrypi/pico-sdk/blob/master/src/host/hardware_divider/include/hardware/divider.h):
  zero-divisor arithmetic.
- [Pico SDK SIO register definitions](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2040/hardware_regs/include/hardware/regs/sio.h):
  divider READY reset state and register semantics.

Run `sysl test .`. Register-level tests cover reset handshakes, masks, all access widths and
ordinary aliases, time latching, counter wrap, alarm equality/past deadlines/rearming/cancellation,
forced and masked interrupts, software/debug pause, the tick's division across every boundary,
exact divider completion/restart/cancel, context restoration, signed and unsigned limits, divide
by zero and independent instances. Guest tests exercise the CPU/peripheral boundary separately.
