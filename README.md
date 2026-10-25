# cortical

A Cortex-M board emulator written in sysl. It runs the firmware a developer would flash onto a board,
unmodified: each core on its own host thread, the board's display and controls in a Mac window, its
UART on a terminal. A board is a description (its chip, parts, wiring, what it exposes and its view),
not a new emulator, and the cores are one interpreter configured per architecture (ARMv6-M, ARMv7E-M,
ARMv8-M Mainline). It is an interpreter; a JIT is not planned.

*An independent sysl project. It is not affiliated with or endorsed by Arm, Raspberry Pi or STMicroelectronics, or their trademark holders; Cortex is a trademark of Arm Limited.*

## Status

**Twelve Cortex-M models run scalar firmware on virtual boards:** M0, M0+, M1, M3, M4, M7,
M23, M33, M35P, M52, M55 and M85. Extensions and limitations are listed below; this is not
TrustZone or Helium emulation. Integer DSP and the floating-point unit (single precision, or double
on the M7) are available through board options. No real board is emulated yet.
[DESIGN.md](DESIGN.md) is the design note: the architecture, the CPU interpreter, each board's
devices, how fidelity is tested against real boards, and the milestones with their estimates.

## The `cortical` command

```
sysl build . -o cortical
./cortical run [--board <name | file.hocon>] [--threads] <image.elf | image.bin>
./cortical check <name | file.hocon>
./cortical bench [--core <core>] <image.bin> [expected r0] [match | threaded]
./cortical bench-board [--board <name | file.hocon>] <image.elf> [turns | threads]
```

**`run`** boots an image on the board `--board` names — the virtual board, `virt`, where none is
named — its console on this terminal a keystroke at a time. It runs a board of two Cortex-M0+ cores,
two Cortex-M4s, two Cortex-M7s or two Cortex-M33s and the parts cortical emulates on their bus, and refuses any
other board with the reason. An ELF file's segments are placed at their physical addresses; any other file is loaded
as raw bytes from address 0. **Ctrl-]** ends cortical whatever the guest is doing, with exit status
130. Otherwise the exit status is the one the guest wrote to the console's EXIT register; a guest
that stops on a breakpoint, an undefined instruction, an access where nothing is, a `WFI` nothing can
wake, a fault the core cannot take, or (on an M33) a stack-limit violation gets a message saying
where, and status 3 for all but the breakpoint. A system reset request (AIRCR) ends it with status 0.

The two cores take turns on one host thread, so a run is the same every time. **`--threads`** gives
each core a host thread of its own once core 1 is launched: faster where both cores are busy, and
not reproducible, as two real cores are not.

**`check`** reads a board or product description and holds it to cortical's part library: it
prints every mistake with its line and column and exits 1, or prints what the description is.

**`bench`** runs a raw image from address 0 to its breakpoint on one core (an M0+, or the core
`--core` names) under both executors, three rounds each, and reports each one's speed;
**`bench-board`** runs an image on a virtual board (`virt`, or the one `--board` names) to its exit,
three rounds with the cores taking turns and three with a thread each, and reports both cores'
speed together. [bench/README.md](bench/README.md) has the numbers.

`sysl test .` runs every test.

## Describing a board

A description is a HOCON file, and the shortest names a board built into cortical:

```hocon
board = pico2w
```

The virtual boards in the core table below, plus `pico`, `pico2w` and `pico-breadboard-kit`,
are built in ([boards](boards) has their sources). **A name with a `/` in it, or ending in `.hocon`, is a file; any other name is built in**,
on the command line and in a file alike, so `--board virt` is the virtual board and `--board
./virt` a file. A path in a file is read from that file's directory.

There are three levels. A **chip** (`rp2040`, `rp2350`) is built into cortical. A **board** names
a chip, or for a board with no real chip its `cores`, and holds its parts, the pins and buses it
exposes, and its view:

```hocon
board {
  name = "Raspberry Pi Pico"
  chip = rp2040
  parts {
    led { part = led, color = green, wire { anode = GP25 } }
  }
  exposes {
    pins  = [GP0, GP1, GP2, GP3, GP4, GP5, GP6, GP7, GP8, GP9, GP10, GP11, GP12, GP13, GP14, GP15,
             GP16, GP17, GP18, GP19, GP20, GP21, GP22, GP26, GP27, GP28]
    buses = [spi0, spi1, i2c0, i2c1, uart0, uart1]
  }
  view {
    size = [168, 408]
    elements { led { show = led, at = [28, 16], size = [16, 10] } }
  }
}
```

A **product** is one or more boards, each under a name of its own, and the parts outside them, wired
only to pins and buses the boards expose, by qualified name. The breadboard kit is a product around a
`pico`:

```hocon
product {
  name = "Pico Breadboard Kit"
  boards { pico { board = pico } }
  parts {
    d1      { part = led, color = red, wire { anode = pico.GP16 } }
    console { part = terminal, wire { uart = pico.uart0 } }
  }
  buses { pico.uart0 { tx = GP0, rx = GP1 } }
}
```

A product tweaks a board by its parts' parameters (`boards { pico { board = pico, parts.led.color =
red } }`) and never otherwise. Each exposed pin is wired once across the product; a part on the
cores' bus has `at`, `size` and `irq`. [DESIGN.md](DESIGN.md) §12 has the whole format and every
mistake `check` reports.

## Core options in a board description

A plain core name uses defaults. An object selects options for that core; omitted options keep
their defaults, independently of the other core:

```hocon
cores = [
  { core = cortex-m7, nvic_priority_bits = 3 }
  { core = cortex-m7, nvic_priority_bits = 8 }
]
```

Both forms can appear in the same list. Existing `cores = [cortex-m7, cortex-m7]` descriptions keep
their behavior. The defaults describe cortical's current integer implementations:

| option | default | accepted selections |
|---|---|---|
| `dsp` | `true` on M4/M7, `false` elsewhere | boolean; always `true` on M4/M7 (ARMv7E-M has it), a choice on M33/M35P/M52/M55/M85 |
| `fpu` | `none` | `sp` on M4/M33/M35P; `sp`/`dp` on M7/M52/M55/M85; `none` on every model |
| `mpu_regions` | `0` | 0 on M0/M1; 0/8 on M0+/M3/M4; 0/8/16 on M7; 0/4/8/12/16 on Armv8-M models |
| `security` | `none` | `trustzone` on M23/M33/M35P/M52/M55/M85; `none` on every model |
| `icache` | `false` | boolean; enabling is valid on M7/M35P/M52/M55/M85 |
| `dcache` | `false` | boolean; enabling is valid on M7/M52/M55/M85 |
| `nvic_priority_bits` | 2 on M0/M0+/M1/M23, 4 otherwise | 2 on Baseline models; 3–8 on Mainline models |
| `mve` | `none` | `none`/`integer`/`float` on M52/M55/M85 |
| `low_overhead_loops` | `false` | boolean on M52/M55/M85 |
| `pacbti` | `false` | boolean on M52/M85 |

**Priority width is implemented.** It controls NVIC and system-handler priorities, BASEPRI and
priority grouping, including after core 1 launches. `check` validates and prints all explicit
options, and writing a description back preserves them. **Integer DSP is implemented** when
`dsp = true`: packed arithmetic, saturation, packing/extension, byte selection and the DSP
multiply/accumulate families, including APSR.GE and exception stacking. It defaults to off.
`fpu = sp` (or `dp` on the M7) gives a core its floating-point unit, whose results and flags
match QEMU's (DESIGN §2.13); the half-precision conversions and `VRINTN`/`VCVTN` are not emulated yet.
`run` currently rejects enabled MPU, TrustZone, caches, MVE, low-overhead loops and PACBTI with a message naming the
unsupported selection; it never silently
ignores them. A chip's integrated core configuration is defined by its chip model; these options
apply to boards that declare `cores` directly.

[examples/virt/core-options.hocon](examples/virt/core-options.hocon) is a runnable M7 example.
[examples/virt/dsp.hocon](examples/virt/dsp.hocon) runs the DSP guest on two M4 cores, which
always have the extension (`dsp = true` there only says so):

```sh
./cortical run --board examples/virt/dsp.hocon examples/virt/dsp.elf
```

The DSP test guest checks 152 instruction variants against QEMU, with integer limits,
accumulator overflow, saturation, NZCVQ and all GE combinations, on both execution engines.
Regenerate the oracle with `zsh examples/qemu/build-dsp.sh` and the guest with
`zsh examples/virt/build.sh`.

## The cores

| architecture | core name | bundled board | implemented scalar ISA |
|---|---|---|---|
| Armv6-M | `cortex-m0` | `virt0` | shared v6-M; fixed vector table at 0 |
| Armv6-M | `cortex-m0plus` | `virt` | existing v6-M implementation, with VTOR |
| Armv6-M | `cortex-m1` | `virt1` | shared v6-M; fixed vector table at 0 |
| Armv7-M | `cortex-m3` | `virt3` | shared integer Thumb-2, IT, exclusives, divide and priority masks |
| Armv7E-M integer | `cortex-m4` | `virt4` | shared v7-M integer ISA plus the DSP extension |
| Armv7E-M integer | `cortex-m7` | `virt7` | same, with the M7's identity and fixed eight-byte exception alignment |
| Armv8-M Baseline | `cortex-m23` | `virt23` | v6-M plus MOVW/MOVT, CBZ/CBNZ, B.W, divide, exclusives and ordered accesses; no IT |
| Armv8-M Mainline subset | `cortex-m33` | `virt33` | shared integer Thumb-2, ordered accesses, stack limits and priority masks |
| Armv8-M Mainline subset | `cortex-m35p` | `virt35p` | same shared scalar ISA as M33; physical tamper resistance is not modelled |
| Armv8.1-M scalar subset | `cortex-m52` | `virt52` | shared v8-M scalar ISA plus CSEL/CSINC/CSINV/CSNEG and their aliases |
| Armv8.1-M scalar subset | `cortex-m55` | `virt55` | same shared scalar subset |
| Armv8.1-M scalar subset | `cortex-m85` | `virt85` | same shared scalar subset |

One interpreter serves all models. The catalog in `v6m/models.sysl` selects identity, architecture
and available configuration options; decoders gate encodings by architecture, and instruction
handlers are shared. M23 is a separate Baseline decoder profile, with two priority bits, word-only
system-register accesses, fixed CCR and no IT, BASEPRI, FAULTMASK or stack limits in its current
configuration without the Security Extension. M0/M1 have no VTOR; M3 has the v7-M register profile.

**All models currently omit the MPU and Security extensions; the floating-point unit is a board option.**
M4 and M7 always have integer DSP; M33/M35P/M52/M55/M85 can enable it independently per core. The M52/M55/M85
models implement a scalar subset, not the full Armv8.1-M ISA: low-overhead loops, long shifts,
MVE/Helium, PACBTI, cache maintenance and TCM control are not emulated. Unsupported instruction
encodings stop at `Undefined`; `run` refuses descriptions enabling unimplemented extensions.
The conditional-select family treats R15 sources as ZR, preserves NZCV/Q, rejects SP and PC
destination operands, and stops on AL/NV constrained-unpredictable conditions.

Every virtual board still implements 32 external interrupts. Architectural maxima are available
to the description checker, but `run` refuses a device selecting IRQ 32 or above. Configurable
faults remain emulator stops rather than implemented fault handlers, as before. Pipeline timing,
physical side-channel resistance, debug/trace and real-chip peripherals are not modelled.

The example build uses matching compiler targets: M0 for M0/M1, M3, M4 and M7 (soft float, so
their DSP instructions are used), M23 for M23, and `armv8-m.main+nodsp` for M35P/M52/M55/M85
(with tuning for the v8.1-M cores). The existing M0+ and M33 guest builds keep their flags. `select.c` adds explicit
v8.1-M conditional-select instructions to scalar compiler output and is checked against QEMU's
M55 by `examples/qemu/build-select.sh`; the 96-instruction shared corpus is also checked on
M3/M35P/M52/M55/M85. The existing v6-M instruction unit cases run on M0/M1/M0+ under both executors.

```
zsh examples/virt/build.sh
./cortical run --board virt23 examples/virt/crunch-m23.elf
./cortical run --board virt35p --threads examples/virt/exclusive-m35p.elf
./cortical run --board virt55 examples/virt/select-v81.elf
```

The M52 model identifies as Arm China's r0p3 (CPUID 0x630FD243), M55 as r1p0 (0x411FD220),
and M85 as r0p0 (0x410FD230). Existing M0+/M4/M7/M33 identities remain unchanged.

The bench kernel (`bench/guest/kernel.c`) built for each, on an M5 Pro:

| core | guest instructions a round | host instructions per guest instruction | cycles per guest instruction |
|---|---|---|---|
| M0+ | 667.5 M | 15.1 | 3.3–3.5 |
| M33 | 587.5 M | 15.3 | 3.6–3.8 |

The M33 build runs 12% fewer instructions in about the same time; on the two-core bench it runs 36%
fewer, in half the time ([bench/README.md](bench/README.md)).

## The virtual boards

Machines that exist only in cortical, for running a program and talking to it with nothing but the
core — deliberately not a real chip. Each board in the core table has two cores of that model,
with the same devices:

| address | what |
|---|---|
| `0x0000_0000`–`0x000F_FFFF` | 1 MB of RAM, shared by both cores: the vector table, code, data and stacks |
| `0x4000_0000` DATA | write: the low byte goes to standard output. read: the next byte typed, or `0xFFFF_FFFF` when none is waiting |
| `0x4000_0004` STATUS | bit 0: a byte is waiting. bit 1: the input has ended and nothing is waiting |
| `0x4000_0008` EXIT | write: cortical ends, with the value as its exit status |
| `0x4000_000C` CTRL | bit 0: the console's interrupt, IRQ 0, is enabled; reads back |
| `0xD000_0000` CPUID | read: 0 on core 0, 1 on core 1 |
| `0xD000_0050` FIFO_ST | bit 0 VLD: this core's RX FIFO holds a word. bit 1 RDY: its TX FIFO has room. bit 2 WOF: it wrote to a full FIFO. bit 3 ROE: it read an empty one. Writing 1 to WOF or ROE clears it |
| `0xD000_0054` FIFO_WR | write: push a word to the other core (8 deep each way); when full the word is lost and WOF set |
| `0xD000_0058` FIFO_RD | read: pop this core's RX FIFO; when empty, 0, and ROE set |
| `0xD000_005C` SPINLOCK_ST | read: bit k set while spinlock k is claimed |
| `0xD000_0100`–`0xD000_017C` SPINLOCK0–31 | read: claim the lock, answering `1 << k` if it was free and 0 if not. write: release it |
| `0xE000_E000`– | each core's own SysTick, NVIC and SCB |

The console raises IRQ 0 while CTRL bit 0 is set and STATUS is not 0 (a byte is waiting, or the input
has ended), in each running core's NVIC. The SIO raises IRQ 1 in a core's own NVIC while its VLD, WOF
or ROE is set. Any other address stops the core, and cortical reports it. Core 0 resets as every
M-profile core does: SP from the vector table's first word, PC from its second. Core 1 waits in a boot ROM until core
0 launches it as on the RP2040 (`multicore_launch_core1_raw`): core 0 pushes 0, 0, 1, VTOR, SP and
ENTRY through the FIFO, each echoed back, and core 1 starts at ENTRY. Each core takes exceptions as its
kind does — SVC, PendSV, SysTick, IRQs, with priorities, PRIMASK and both stacks, and on an M33
BASEPRI, FAULTMASK and the stack limits; SysTick counts one per instruction. [DESIGN.md](DESIGN.md)
§2.2 and §2.9 say what is modelled and what is not.

**Both cores run on one host thread, taking turns** of 1000 instructions each, so a run is the same
every time, to the instruction. A core asleep in `WFI` costs nothing while the other runs, and the
board stops at `Asleep` only when both sleep with nothing that can wake them. A program that never
launches core 1 runs exactly as on a single core. [DESIGN.md](DESIGN.md) §1.1 and §11 have the rest.

**With `--threads` each core runs on a host thread of its own**, and neither gets more than 4000
instructions ahead of the other; a core asleep in `WFI` blocks its thread until the other core or its
own SysTick wakes it. On the two-core bench that is about 1.8 times the speed of taking turns
([bench/README.md](bench/README.md)). Guest RAM accesses are the host's relaxed atomic accesses and
`DMB`/`DSB`/`ISB` a host fence, so a program that orders its accesses with barriers, or through the
SIO's spinlocks and FIFOs, sees them in order; code one core writes is run by the other after the
writer's `DSB` and the runner's `ISB`. [DESIGN.md](DESIGN.md) §1.3–§1.5 give the argument.

[examples/virt](examples/virt) is a C runtime for it (`virt.h`, `start.c`, `virt.ld`), `echo.c`,
which echoes each line typed until the input ends or Ctrl-D, `irq.c`, which echoes from the
console's interrupt handler while `main` sleeps in `WFI`, and `dual.c`, in which core 0 launches core
1, sends it numbers through the FIFO to be squared in core 1's FIFO interrupt, and both cores print
under a spinlock. `count.c`, `pingpong.c` and `order.c` are what the tests run on two threads: both
cores counting under one spinlock, the FIFO run full both ways, and messages passed through RAM with
`DMB`s. `crunch.c` is integer arithmetic a compiler makes most of the instruction set of — 64-bit
division, bitfields, switch tables, saturation, conditional selection — and prints what the same C
built for the host prints (`crunch.out`). For the M33 alone, `exclusive.c` has both cores add to
shared counters with no lock, through `LDREX`/`STREX`, and `isa.c` hashes 96 instructions over edge
values and flag states, which QEMU's Cortex-M33 hashes the same (`isa.qemu`, made by
`examples/qemu/build.sh`). A program handles an exception by defining the function `start.c`'s
vector table names (`console_handler`, `sio_handler`, `systick_handler`, `svc_handler`, …). Each
program is built for `virt` as `<program>.elf` and for `virt33` as `<program>-m33.elf`; build them
with `arm-none-eabi-gcc` on the PATH:

```
zsh examples/virt/build.sh
./cortical run examples/virt/echo.elf
./cortical run examples/virt/irq.elf
./cortical run examples/virt/dual.elf
./cortical run --threads examples/virt/dual.elf
./cortical run --board virt33 examples/virt/crunch-m33.elf
./cortical run --board virt33 --threads examples/virt/exclusive-m33.elf
./cortical run --board examples/virt/moved.hocon examples/virt/irq-moved.elf
```

The last runs `irq.c` built for a console at `0x5000_0000` on IRQ 7, on a description of the board
that puts it there: the devices are wherever the description says.

## Boards, in order

1. **Raspberry Pi Pico 2** (RP2350, two Cortex-M33s): one core and UART first, then both cores.
2. **Raspberry Pi Pico with the GeeekPi 3.5" breadboard kit** (RP2040, two Cortex-M0+s; 52Pi
   EP-0172): the ST7796 screen, GT911 touch, joystick, buttons, LEDs, the WS2812 RGB LED and the
   buzzer, running [picokit](https://github.com/sysl-lang/picokit) unmodified.
3. **STM32H747I-DISCO** (a Cortex-M7 and a Cortex-M4): UART, both cores, then the 800×480 DSI
   display.

After each board, [skiff](https://github.com/sysl-lang/skiff) runs on it. Later: the micro:bit v1 and
v2, PIO for the Pico 2, the Pico W radio and USB.

The oracle is always the real board running the same image, never cortical's own idea of it.

## Licence

ISC.
