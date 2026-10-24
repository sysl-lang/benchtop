# cortical

A Cortex-M board emulator written in sysl. It runs the firmware a developer would flash onto a board,
unmodified: each core on its own host thread, the board's display and controls in a Mac window, its
UART on a terminal. A board is a description (cores, memory, devices, wiring, panel), not a new
emulator, and the cores are one interpreter configured per architecture (ARMv6-M, ARMv7E-M,
ARMv8-M Mainline). It is an interpreter; a JIT is not planned.

*An independent sysl project. It is not affiliated with or endorsed by Arm, Raspberry Pi or STMicroelectronics, or their trademark holders; Cortex is a trademark of Arm Limited.*

## Status

**The ARMv6-M (Cortex-M0+) core runs, on a virtual board.** No real board is emulated yet.
[DESIGN.md](DESIGN.md) is the design note: the architecture, the CPU interpreter, each board's
devices, how fidelity is tested against real boards, and the milestones with their estimates.

## The `cortical` command

```
sysl build . -o cortical
./cortical run <image.elf | image.bin>
./cortical bench <image.bin> [expected r0] [match | threaded]
```

**`run`** boots an image on the virtual board, its console on this terminal a keystroke at a time.
An ELF file's segments are placed at their physical addresses; any other file is loaded as raw bytes
from address 0. **Ctrl-]** ends cortical whatever the guest is doing, with exit status 130. Otherwise
the exit status is the one the guest wrote to the console's EXIT register; a guest that stops on a
breakpoint, an undefined instruction or an access where nothing is gets a message saying where, and
status 3 for the last two.

**`bench`** runs a raw image from address 0 to its breakpoint under both executors, three rounds
each, and reports each one's speed; [bench/README.md](bench/README.md) has the numbers.

`sysl test .` runs every test.

## The virtual board

A machine that exists only in cortical, for running a program and talking to it with nothing but the
core — deliberately not a real chip. One Cortex-M0+ core:

| address | what |
|---|---|
| `0x0000_0000`–`0x000F_FFFF` | 1 MB of RAM: the vector table, code, data and stack |
| `0x4000_0000` DATA | write: the low byte goes to standard output. read: the next byte typed, or `0xFFFF_FFFF` when none is waiting |
| `0x4000_0004` STATUS | bit 0: a byte is waiting. bit 1: the input has ended and nothing is waiting |
| `0x4000_0008` EXIT | write: cortical ends, with the value as its exit status |

Any other address stops the core, and cortical reports it. The core resets as an M0+ does: SP from
the vector table's first word, PC from its second.

[examples/virt](examples/virt) is a C runtime for it (`virt.h`, `start.c`, `virt.ld`) and `echo.c`,
which echoes each line typed until the input ends or Ctrl-D. Build it with `arm-none-eabi-gcc` on the
PATH:

```
zsh examples/virt/build.sh
./cortical run examples/virt/echo.elf
```

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
