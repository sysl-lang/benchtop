# cortical

A Cortex-M board emulator written in sysl. It runs the firmware a developer would flash onto a board,
unmodified: each core on its own host thread, the board's display and controls in a Mac window, its
UART on a terminal. A board is a description (cores, memory, devices, wiring, panel), not a new
emulator, and the cores are one interpreter configured per architecture (ARMv6-M, ARMv7E-M,
ARMv8-M Mainline). It is an interpreter, shaped so that a JIT can replace the inner loop later.

*An independent sysl project. It is not affiliated with or endorsed by Arm, Raspberry Pi or STMicroelectronics, or their trademark holders; Cortex is a trademark of Arm Limited.*

## Status

**Design; nothing runs yet.** [DESIGN.md](DESIGN.md) is the design note: the architecture, the CPU
interpreter, each board's devices, how fidelity is tested against real boards, and the milestones
with their estimates.

## Boards, in order

1. **Raspberry Pi Pico 2** (RP2350, two Cortex-M33s): one core and UART first, then both cores.
2. **Raspberry Pi Pico with the GeeekPi 3.5" breadboard kit** (RP2040, two Cortex-M0+s; 52Pi
   EP-0172): the ST7796 screen, GT911 touch, joystick, buttons, LEDs, the WS2812 RGB LED and the
   buzzer, running [picokit](https://github.com/sysl-lang/picokit) unmodified.
3. **STM32H747I-DISCO** (a Cortex-M7 and a Cortex-M4): UART, both cores, then the 800×480 DSI
   display.

After each board, [skiff](https://github.com/sysl-lang/skiff) runs on it. Later: the micro:bit v1 and
v2, PIO for the Pico 2, the Pico W radio, USB, and a JIT.

The oracle is always the real board running the same image, never cortical's own idea of it.

## Licence

ISC.
