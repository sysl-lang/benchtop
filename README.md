# cortical

A Cortex-M board emulator written in sysl. It runs the firmware a developer would flash onto a board,
unmodified: each core on its own host thread, the board's display and controls in a Mac window, its
UART on a terminal. A board is a description (its chip, parts, wiring, what it exposes and its view),
not a new emulator, and the cores are one interpreter configured per architecture (ARMv6-M, ARMv7E-M,
ARMv8-M Mainline). It is an interpreter; a JIT is not planned.

*An independent sysl project. It is not affiliated with or endorsed by Arm, Raspberry Pi or STMicroelectronics, or their trademark holders; Cortex is a trademark of Arm Limited.*

## Status

**The ARMv6-M (Cortex-M0+) and ARMv8-M Mainline (Cortex-M33, its integer instruction set) cores
run, on the virtual boards.** No real board is emulated yet.
[DESIGN.md](DESIGN.md) is the design note: the architecture, the CPU interpreter, each board's
devices, how fidelity is tested against real boards, and the milestones with their estimates.

## The `cortical` command

```
sysl build . -o cortical
./cortical run [--board <name | file.hocon>] [--threads] <image.elf | image.bin>
./cortical check <name | file.hocon>
./cortical bench [--core <cortex-m0plus | cortex-m33>] <image.bin> [expected r0] [match | threaded]
./cortical bench-board [--board <name | file.hocon>] <image.elf> [turns | threads]
```

**`run`** boots an image on the board `--board` names — the virtual board, `virt`, where none is
named — its console on this terminal a keystroke at a time. It runs a board of two Cortex-M0+ cores
or two Cortex-M33s and the parts cortical emulates on their bus, and refuses any other board with
the reason. An ELF file's segments are placed at their physical addresses; any other file is loaded
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

`virt`, `virt33`, `pico`, `pico2w` and `pico-breadboard-kit` are built in ([boards](boards) has their
sources). **A name with a `/` in it, or ending in `.hocon`, is a file; any other name is built in**,
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

## The cores

| kind | core | what it runs |
|---|---|---|
| ARMv6-M | `cortex-m0plus` | the whole ARMv6-M instruction set, its exceptions, SysTick, the NVIC and SCB |
| ARMv8-M Mainline | `cortex-m33` | the integer Thumb-2 instruction set: every 16- and 32-bit integer encoding, IT blocks, the exclusives, `LDA`/`STL`, hardware divide; BASEPRI, FAULTMASK, PRIGROUP, the stack limits (MSPLIM, PSPLIM) |

One interpreter serves both, configured by the core's kind when it decodes; the M0+ runs no slower
for the M33 being there. The M33 has no FP, DSP, Security or MPU extension — their encodings are
undefined, so a program for it is built `-mcpu=cortex-m33+nodsp -mfloat-abi=soft` — and takes no
configurable fault: where a UsageFault, BusFault or MemManage would be taken, the core stops and
cortical says why. Each is checked against the ARMv8-M or ARMv6-M ARM instruction by instruction, and
the M33 against QEMU's too ([DESIGN.md](DESIGN.md) §2.8–§2.9).

The bench kernel (`bench/guest/kernel.c`) built for each, on an M5 Pro:

| core | guest instructions a round | host instructions per guest instruction | cycles per guest instruction |
|---|---|---|---|
| M0+ | 667.5 M | 15.1 | 3.3–3.5 |
| M33 | 587.5 M | 15.3 | 3.6–3.8 |

The M33 build runs 12% fewer instructions in about the same time; on the two-core bench it runs 36%
fewer, in half the time ([bench/README.md](bench/README.md)).

## The virtual boards

Machines that exist only in cortical, for running a program and talking to it with nothing but the
core — deliberately not a real chip. `virt` has two Cortex-M0+ cores and `virt33` two Cortex-M33s,
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
