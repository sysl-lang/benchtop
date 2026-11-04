# Debugging a guest with gdb

`benchtop run --gdb` serves the GDB Remote Serial Protocol over TCP, so `arm-none-eabi-gdb` (or
`gdb-multiarch`, or anything that drives gdb, such as VS Code's Cortex-Debug) connects straight to the
emulator. There is no probe in between: gdb's packets reach the emulated cores themselves.

```
benchtop run --board stm32h747-disco --gdb firmware.elf
benchtop run --board pico --gdb 4444 blink.elf
```

`--gdb` takes an optional port, 3333 when none is given. **Each core has a port of its own**, from that
one up, as OpenOCD numbers a dual-core chip's:

| board | port | core |
|---|---|---|
| STM32H747I-DISCO | 3333 | core 0, the Cortex-M7 |
| | 3334 | core 1, the Cortex-M4 |
| Pico (RP2040), Pico 2 (RP2350, Arm) | 3333, 3334 | cores 0 and 1 |
| the virtual boards | 3333, 3334 | cores 0 and 1 |

The ports listen on the loopback (127.0.0.1) only. benchtop prints them on stderr, and **the board waits
at its reset vector until a gdb resumes it**, so `break main` before the first `continue` is never too
late. The guest's console stays on benchtop's own terminal, as in a plain `benchtop run`.

`--gdb` is for a run without `--window`, and for the Cortex-M boards; `--usb pty` is not taken with it
(`--usb console` and `--usb off` are).

## A session

```
arm-none-eabi-gdb firmware.elf
(gdb) target remote :3333
(gdb) break main
(gdb) continue
(gdb) info registers
(gdb) x/4wx $sp
(gdb) stepi
(gdb) continue
^C
(gdb) detach
```

The second core is a second gdb: `target remote :3334` from another terminal, with the M4's ELF on the
H747.

### VS Code (Cortex-Debug)

Cortex-Debug's `external` server type connects gdb to a port it does not start:

```json
{
  "name": "benchtop: core 0",
  "type": "cortex-debug",
  "request": "attach",
  "servertype": "external",
  "gdbTarget": "localhost:3333",
  "executable": "${workspaceFolder}/build/firmware.elf",
  "gdbPath": "arm-none-eabi-gdb"
}
```

Start `benchtop run --gdb firmware.elf` first (a `preLaunchTask` can), then attach.

## How the board stops

**All-stop.** The board runs while any attached gdb has resumed its core, and it stops as a whole —
both cores — when:

- a core reaches a breakpoint its own gdb set: that gdb hears `T05` with `swbreak` (or `hwbreak` for an
  `hbreak`), and the pc is the breakpoint's, the instruction not yet run;
- a core stops for any other reason: the guest's own `BKPT` (`T05`), an undefined instruction (`T04`,
  SIGILL), or a fault, an access nothing answers or a lockup (`T0b`, SIGSEGV);
- a gdb sends Ctrl-C (`T02`, SIGINT);
- a gdb attaches.

A gdb whose core was running is told `T02` when the board stopped on the other core's account. **A step
runs only the stepped core's one instruction**; the other core does not move. Stepping a core asleep in
`WFI` runs the board until something wakes it. A pending, enabled interrupt is taken by a step, as on
the hardware: the step stops at its handler's first instruction.

**A breakpoint is its core's.** Breakpoints change no byte of the guest's memory — benchtop marks the
instruction where it keeps its decoded code — so one set in flash or ROM is no different from one in
RAM, and `x` at its address still shows the guest's instruction. A core that reaches a breakpoint only
the *other* core's gdb set runs on past it. Up to 64 are held at once, both cores together; `break` and
`hbreak` are the same mechanism. On a Cortex-M33, M4 or M7, a breakpoint on an instruction inside an IT
block stops there only when the instruction's condition holds.

**The clock.** Under gdb the board is not paced to the host's clock: it runs as fast as the host lets
it, as `benchtop run` does, and the guest's time is its cores' cycles. A stop holds every clock exactly
where it is, devices included, and a resume goes on from there; nothing catches up afterwards.

**Ending.** `detach` lets the board run on with no gdb, as a target does when its probe lets go; a gdb
may attach again later. If a core then stops with no gdb attached, benchtop ends as `benchtop run`
would (0 at the guest's `BKPT`, 3 at anything else). `kill` ends benchtop. A guest that exits through
the virtual board's console reports its status to every attached gdb (`W`), and benchtop exits with it.

## What a read of a device does

**Memory is read and written through the core's own bus**, exactly as a load or store of the guest's
would be: RAM, flash and ROM from the memory, a peripheral's registers from the peripheral, and the
core's own system control space (NVIC, SysTick, SCB) from that core. **A read of a register with a side
effect has it** — reading a UART's data register or a FIFO pops a word, as it does over a real probe's
SWD. gdb reads only what it is asked to, so `x` on a peripheral is the one way to cause it; benchtop
does not peek. Each read uses the widest aligned access that fits (`x/4wx` reads four words, never
sixteen bytes). An address nothing answers is an error (`Cannot access memory`), and leaves the core as
it was. Flash refuses a write, as it refuses the guest's; a write to RAM that holds code makes benchtop
decode it again.

## Registers

The target description (`qXfer:features:read`) is the core's own:

- `org.gnu.gdb.arm.m-profile`: r0–r12, sp, lr, pc, xpsr;
- `org.gnu.gdb.arm.m-system`: msp, psp, primask, basepri and faultmask (not on an M0+), control;
- `org.gnu.gdb.arm.vfp` where the core has an FPU (the H747's M7 and M4): d0–d15, fpscr; gdb shows s0–s31
  as their halves.

Writing xpsr sets the flags (and Q and GE); the exception number, Thumb bit and IT state are the core's
and stay. Writing control's SPSEL in thread mode changes which stack `sp` is, as `MSR CONTROL` does.

## Packets

`qSupported` (`PacketSize=4000`, `qXfer:features:read+`, `QStartNoAckMode+`, `swbreak+`, `hwbreak+`),
`qXfer:features:read:target.xml`, `QStartNoAckMode`, `?`, `g`, `G`, `p`, `P`, `m`, `M`, `X`, `c`, `C`,
`s`, `S`, `vCont?` and `vCont` (`c`, `C`, `s`, `S`, `t`), `Z0`/`z0`, `Z1`/`z1`, Ctrl-C, `D`, `k`,
`vKill`, `qAttached`, `qC`, `qfThreadInfo`/`qsThreadInfo` (one thread per port), `H` and `T`. Any other
packet is answered empty, which gdb reads as unsupported. benchtop never sends run-length encoding.

## Not yet

- watchpoints (`Z2`–`Z4`, `watch`, `rwatch`, `awatch`);
- semihosting, ITM/SWO trace, and the CoreSight debug registers (DHCSR, DEMCR, the FPB and DWT) as the
  guest would see them;
- `monitor` commands (`qRcmd`), among them `monitor reset`;
- `load` into flash;
- `--gdb` with `--window`, which runs the board paced on its own threads;
- the RP2350's Hazard3 (RISC-V) harts, which need breakpoints in their decoder, a step, and a RISC-V
  target description.
