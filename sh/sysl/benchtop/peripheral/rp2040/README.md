# RP2040 peripheral models

Reusable, instance-owned models for RESETS, TIMER and the SIO integer divider. They import the
SVD-generated `github.com/sysl-lang/rp2040` register constants. No model imports a CPU module,
uses global device state, starts a host thread, or reads the host clock.

These components are ready for chip-layer composition. They are not yet registered as runnable
board parts, and do not make `--board pico` runnable. GPIO/pads, UART, clock generators, watchdog,
DMA, SPI, I2C, PWM, PIO, USB, interpolators, flash and boot ROM are separate work.

## Chip integration contract

Construct with `resets()`, `timer()` and `divider()`, or explicitly call `reset()` on fresh storage.
All register methods take a relative offset, a transfer width in bytes (1, 2 or 4), and, for writes,
the value. Reads return `Result[u32, AccessError]`; writes return `Result[unit, AccessError]`.
The chip maps addresses, routes errors to its fault/diagnostic policy, and serializes shared
peripheral access across CPU threads. Reset and clock delivery must use the same serialization.
There are no internal locks and copying a struct copies its complete device state.

- **RESETS:** one shared `Resets` instance. `held` is the 25-bit reset-output mask; `watchdog` is
  WDSEL. After a RESET write, the chip applies asserted resets to connected devices, releases the
  others and calls `complete(bits)` when those devices are ready. RESET_DONE stays low until that
  handshake; writing a reset bit clears its done bit. Reset completion is deliberately not faked
  for peripherals the chip has not instantiated. The chip's watchdog model consumes WDSEL when
  it delivers a watchdog reset.
- **TIMER:** one shared `Timer`. Supply watchdog-generated timer ticks with `advance(ticks,
  halted)`, where `halted` bit 0/1 indicates the debug-halted state of each processor. No tick
  delivery means no counter progress, including when WATCHDOG_TICK is disabled. The chip must
  split clock advancement at writes or debug-state changes. `irqs()` is a four-bit level output,
  normally routed to RP2040 IRQs 0–3 on both processors. `next_alarm(halted)` returns an optional
  distance in input ticks, allowing a scheduler to advance while the CPUs sleep. It does not
  claim that the watchdog tick source is running, or that an interrupt is enabled in either CPU.
- **Divider:** two `Divider` instances, one per processor. Dispatch the SIO offsets 0x60–0x78 to
  the requesting processor's instance, alongside the shared SIO implementation. Supply that
  divider's input clocks with `advance(cycles)`. `remaining` is the distance to completion, 0
  when ready. Every operand write restarts eight clocks; result writes cancel pending work.
  Read the remainder before the quotient when preserving DIRTY for SDK context-save logic.

The peripheral methods do not assert CPU pending bits themselves. In particular, the chip must
resample level IRQs at scheduling/exception boundaries as well as after register writes.
`v6m/rp2040_peripheral_tests.sysl` supplies a test-only bus demonstrating reset fanout, guest MMIO,
alarm IRQ entry/acknowledgement/return, and independent core-local divider instances with both CPU
execution engines. Its explicit clock delivery is not a complete RP2040 scheduler.

## Register behavior and boundaries

TIMER implements staged time writes, the shared high-word read latch, raw reads, four one-shot
comparators, cancellation, raw/enable/force/status interrupt registers, software pause and debug
pause. Advancing over multiple low-word wraps still finds armed alarms. A time-register write
compares only the newly written value; it is not elapsed time. Alarms programmed equal to the
counter match immediately. Each fired alarm disarms and latches its raw interrupt independently
of the interrupt-enable mask.

RESETS and TIMER implement RP2040 replicated byte/halfword writes, little-endian narrow reads,
and the +0x1000/+0x2000/+0x3000 XOR/SET/CLR aliases for ordinary RW registers. Reads through these
aliases have the same effects as a base read. To avoid inventing unverified hardware behavior,
atomic-alias writes to TIMER's write-only time registers and write-one-to-clear ARMED/INTR return
`UnsupportedAlias`. Normal writes to those registers are implemented. Read-only writes are
ignored; write-only time reads return zero. Unknown registers, invalid widths and unaligned
transfers return explicit errors without changing state. SIO has no bus atomic aliases.

Divider results during a busy calculation are architecturally unspecified; this model retains
the old results until completion. Signed division uses truncation toward zero and the dividend's
remainder sign. INT_MIN/-1 wraps to INT_MIN. Zero-divisor behavior follows the Pico SDK's host
implementation: unsigned quotient -1; signed quotient minus the sign of the dividend (including
zero for 0/0); remainder equal to the dividend. Physical-board differential validation of these
edge behaviors, comparator synchronization latency and bus-cycle timing remains outstanding.

## Sources and validation

- [RP2040 datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf): sections
  2.1.2–2.1.4 (bus aliases/narrow accesses), 2.3.1.5 (divider), 2.14 (resets), 4.6 (timer).
- [Pico SDK host divider](https://github.com/raspberrypi/pico-sdk/blob/master/src/host/hardware_divider/include/hardware/divider.h):
  zero-divisor arithmetic.
- [Pico SDK SIO register definitions](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2040/hardware_regs/include/hardware/regs/sio.h):
  divider READY reset state and register semantics.

Run `sysl test .`. Register-level tests cover reset handshakes, masks, all access widths and
ordinary aliases, time latching, counter wrap, alarm equality/past deadlines/rearming/cancellation,
forced and masked interrupts, software/debug pause, exact divider completion/restart/cancel,
context restoration, signed and unsigned limits, divide by zero and independent instances.
Guest tests exercise the CPU/peripheral boundary separately from these register tests.
