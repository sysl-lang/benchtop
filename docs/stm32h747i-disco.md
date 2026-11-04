# STM32H747I-DISCO emulation target

This is the board being implemented, not an assertion that the complete board
already runs. The existing SPI touchscreen example remains runnable during
bring-up. The board must use real STM32 register interfaces and flash-linked
Sysl firmware; replacing names on virtual devices is insufficient.

## Workload assignment

- Cortex-M7: audio processing and audio input/output, target 480 MHz.
- Cortex-M4: imui, display rendering and touch input, target 240 MHz.
- Each guest core runs on its own persistent host thread. UI rendering and
  instruments remain on the desktop side, reading safe snapshots.
- Inter-core communication uses shared memory and hardware synchronization.
  No host-only callbacks may replace guest firmware communication.

## Hardware target

The STM32H747XIH6 has 2 MB internal flash and 1 MB of RAM split into distinct
banks. The bank map and access permissions matter: neither RAM nor the
peripherals should be represented as one fictitious contiguous region.

The Discovery board adds 32 MB IS42S32800J SDRAM at 0xD0000000 and two 512-Mbit
Quad-SPI flash devices. External memory belongs in the parts catalog, attached to
FMC/Quad-SPI peripherals on the MCU.

The MB1166 display is 800x480 with a DSI controller (NT35510 or OTM8009A,
depending on module revision) and an FT6x06-family capacitive touch controller.
The display path comprises a guest framebuffer, LTDC, the DSI host, and the
panel. The framebuffer and transfer engines must remain available to either CPU;
putting the graphics workload on M4 is a firmware decision. DMA2D is a distinct
hardware graphics accelerator, not a replacement for instruction emulation.

Audio output uses SAI and an I2C-controlled codec. Audio inputs include onboard
digital microphones. M7 firmware should configure the real peripherals and
transfer PCM through their FIFOs/DMA. Existing host PCM playback/capture and
buffering should be reused, with the hardware adapter supplying samples.

Board/display revisions affect actual component identities and addresses; the
chosen revision must be explicit when the board description is introduced.

## Implemented foundation

- Heterogeneous Cortex-M pairs, including M7/M4, boot/reset/launch with their own
  core identities, DSP and FPU configuration.
- Separate instruction decode records for different cores, including serial
  deterministic execution; shared-code writes still invalidate peer records.
- Per-core `clock-hz` overrides on a shared MCU timebase. Current clock domains
  require integer divisors (480 MHz / 240 MHz is supported).
- Local SysTick timing, shared peripheral deadlines, persistent worker pacing,
  snapshots, pause and exact instruction stepping.
- Regression coverage for 2:1 busy-loop progress in both schedulers, local
  SysTick wakeup conversion, invalid clocks, reset and differing architectures.

## Runnable dual-core bring-up harness

```
./examples/touch/build.sh h7
./benchtop run --window --board boards/h747-cores.hocon examples/touch/h7.elf
```

This runs actual M7 and M4 guest cores. M7 initializes the shared runtime and
uses `RCC_GCR.BOOT_C2` to release M4 from its vector table at 0x08100000.
The image targets the common ARMv7E-M single-precision subset, with M4-compatible
newlib and PlutoVG; both vectors and code reside in the real internal-flash
address range. Runtime data uses 512 KB AXI SRAM, with separate stacks.

The boot configuration corresponds to BCM4 disabled in option bytes. The current
`stm32h7-rcc` adapter implements GCR boot control and the peripheral clock enables;
oscillator, PLL, reset (`xxxRSTR`), sleep-mode enable (`xxxLPENR`) and other RCC
accesses fail explicitly. Clock rates are supplied by the description.

The enables are the nine registers AHB3ENR, AHB1ENR, AHB2ENR, AHB4ENR, APB3ENR, APB1LENR,
APB1HENR, APB2ENR and APB4ENR, in both cores' copies: `RCC_C1_xxxENR` at 0x134-0x154 (the
M7) and `RCC_C2_xxxENR` at 0x194-0x1B4 (the M4), which either core may read and write. The
plain `RCC_xxxENR` at 0xD4-0xF4 is the accessing core's own copy. Each starts at zero
except APB4ENR, whose RTCAPBEN is set from reset (0x00010000). A write that sets a bit
the register does not implement (per ST's `stm32h747xx.h`) faults, like any other
unsupported write. **The enable bits are stored; peripherals run regardless**: no
peripheral model is gated by its clock, so firmware that forgets an enable still works
here and will not on the board.

The UI executes on M4. In images built without the `audio` feature M7 only spins;
`audio-disco.elf` (built with `audio`) runs sine DSP on M7 and sleeps in `wfi`
between interrupts. The screen/touch are still the existing
ST7789/GT911 test harness using virtual peripherals. This is **not** the complete
Discovery board, and the window labels it as core bring-up. The original M33
board and all original SPI/GPIO firmware modes remain available.

## Runnable Discovery DSI subset

```
./examples/touch/build.sh dsi
./benchtop run --window --board stm32h747-disco examples/touch/disco.elf
```

The built-in description lives in `boards/stm32h747-disco.hocon`, on the built-in mcu
`stm32h747` (`mcus/stm32h747.hocon`), which holds the chip's facts: the real internal
flash and AXI SRAM addresses, RCC CM4 release, STM32 DSI at 0x50000000, I2C4 at
0x58001c00, SDMMC1, SPI1, GPIO A to K with their reset modes, and the pins each signal
can be routed to. The board picks I2C4's pins, PD12/PD13, under `buses`;
`stm32h747-sd.hocon` and `stm32h747-spi-sd.hocon` are the same board with a card. An FT6206 at address 0x2a supplies touch
reports.

The four user LEDs, LD1 to LD4, are on PI12 to PI15 and light while their pin is an output driven
low. The wake-up button B2 holds PC13 high while pressed (key 1); the reset button B1 (key 2)
resets the chip as it goes down, and the chip stays in reset until it is let go. The speaker on the
codec's speaker output is a Same Sky CMS-151103-088SP, a 15 x 11 mm micro speaker: below its
850 Hz resonance its cone rolls off at 12 dB an octave, so it sounds thin where headphones do not. The display part implements common MIPI DCS behavior rather
than a complete NT35510/OTM8009A vendor-specific controller.

M4 sends real DCS short/long packets through GHCR/GPDR. The host emulates their
completion and updates the separate display model. Host painting does not write
pixels into the device. Packet timing uses PLL-derived lane rate, lane count and
packet length; PHY transitions, LP/HS timing overhead, TE and ECC/CRC faults are
not modeled. The hardware's long-packet length is 16 bits, but the packet payload
limit is 1024 bytes, rejected above that bound.

The example keeps its 240x240 imui surface centered on the 800x480 native glass.
The full glass is initialized over DSI. Interactions arrive through FT6206
registers and the STM32 I2C v2 polling adapter; no virtual SPI or I2C peripherals
are used. Two FT6206 contacts are modeled; desktop pointer input supplies one.
I2C DMA, IRQ, slave operation and SCL timing remain unsupported.

The demonstration ELF has real flash load addresses and ARMv7E-M instructions,
but complete physical-board startup is not ready: RCC oscillator/PLL setup, pin-mux setup
and the panel-specific vendor initialization sequence remain to be implemented
before this example can boot unchanged on a physical Discovery board.

## ST-LINK virtual COM port (USART1)

The ST-LINK's virtual COM port is USART1 at 0x40011000, IRQ 37, its TX on PA9 and RX on PA10 in
alternate function 7 (`buses { usart1 { tx = PA9, rx = PA10 } }`); the board's `vcp` part is the
host's terminal on its line. Its kernel clock is 120 MHz, rcc_pclk2 with APB2 at 120 MHz, the
reset choice of USART16SEL. What the guest transmits joins the console's output, at the baud rate
BRR and PRESC set; what is typed into the console reaches the receiver, one character a frame. A
character crosses only while its pin is in AF7, as on the board: a guest that leaves PA9 analogue
sends to no one, and typed input waits in the console while the receiver is off or PA10 is not
routed. A core in WFI with RXNEIE set is woken by typing; with the input ended, or with nothing
enabled that typing would raise, it is reported asleep.

The register model is the STM32H7 USART in asynchronous mode: 7, 8 or 9 data bits with even or odd
parity, 0.5 to 2 stop bits, 16 or 8 times oversampling and the prescaler, the 16-deep FIFOs and their
thresholds, and the flags and interrupts a polling or interrupt-driven driver uses. What it refuses
is listed in [peripheral models](peripheral-models.md). Benchtop runs one STM32 USART on a board;
a second is refused, as is a `stm32-usart` without `kernel-clock-hz` and a terminal on a USART
whose TX and RX are not routed to pins of the chip's GPIO.

### Forth example

`examples/disco-repl` is a Forth interpreter (the sysl-lang `forth` package) on the virtual COM
port: USART1 on PA9/PA10 in AF7, receive interrupt enabled, the core in WFI between characters.
Build it with `sh examples/disco-repl/build.sh` (needs `arm-none-eabi-ld`); the result,
`repl-disco.elf`, is checked in. Run it with a terminal on stdin:

```
./benchtop run --board stm32h747-disco examples/disco-repl/repl-disco.elf
```

Each typed line is echoed and interpreted; backspace edits the line. Besides the standard words,
the startup defines `LED-ON` and `LED-OFF` (take an LED number 1 to 4, LD1 to LD4, writing GPIOI
BSRR) and `BUTTON?` (pushes the state of B2, read from GPIOC IDR). When stdin ends the guest is
asleep in WFI with nothing to wake it, which `run` reports and exits with status 3. A session:

```
forth on the STM32H747I-DISCO
2 3 + .
5  ok
: SQ DUP * ; 7 SQ .
49  ok
1 LED-ON
 ok
BUTTON? .
0  ok
```

## Remaining board bring-up

Real memory banks and external SDRAM; remaining STM32 RCC/PWR behavior;
GPIO and full I2C timing/interrupt behavior;
LTDC video streaming and complete Discovery panel-specific initialization; WM8994
DMIC2/analog capture and complete physical-board startup. The optional
`audio-disco.elf` image runs sine DSP on M7 with WM8994 speaker playback and M4
touch controls; see the audio peripheral documentation for the supported subset.
SAI, DMA and DMAMUX register mappings and their real interrupt numbers are
implemented; see [audio peripherals](stm32-audio-peripherals.md).

The demo selects fixed per-instruction cycle estimates for both cores.
Unit timing remains available through `instruction-timing = unit`. See
[instruction timing](instruction-timing.md) for constants and limitations. This
does not model M7 superscalar issue, caches, bus bandwidth or flash wait states.
Raw interpreter throughput is evidence of host headroom, not a guarantee that
audio firmware will match the execution time of a physical H747.

## Real time

`./benchtop realtime examples/touch/mic-disco.elf` runs the microphone variant on both cores'
workers with no window, the DSI display and the audio in both directions running, and reports each
core's speed against real time; [bench](../bench/README.md#real-time-on-the-stm32h747i-disco) has the
numbers and what they show. `examples/touch/busy-disco.elf` is the same check with an M7 that never
sleeps (memory, double precision, branches and peripheral polls between its audio interrupts), and
`--set busy_kernel=N` runs one of those alone.

## Primary references

- [ST board user manual UM2411](https://www.st.com/resource/en/user_manual/um2411-discovery-kit-with-stm32h747xi-mcu-stmicroelectronics.pdf)
- [ST dual-core architecture AN5557](https://www.st.com/resource/en/application_note/dm00733995-stm32h745755-and-stm32h747757-lines-dualcore-architecture-stmicroelectronics.pdf)
- [ST board support package](https://github.com/STMicroelectronics/stm32h747i-disco-bsp)
- [ST LCD driver](https://github.com/STMicroelectronics/stm32h747i-disco-bsp/blob/main/stm32h747i_discovery_lcd.c)
- [ST touch interface](https://github.com/STMicroelectronics/stm32h747i-disco-bsp/blob/main/stm32h747i_discovery_ts.h)
- [ST SDRAM interface](https://github.com/STMicroelectronics/stm32h747i-disco-bsp/blob/main/stm32h747i_discovery_sdram.h)

The microphone variant (`./examples/touch/build.sh mic`, `mic-disco.elf`) adds
host microphone capture through WM8994 DMIC1, SAI1 B and DMA2 stream4. M7 applies
dry/low-pass/high-pass processing and fills the speaker TX buffer from receive
interrupts; M4 draws the input meter and handles monitoring controls. PDM
decimation is idealized and MONITOR starts disabled.
