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

## Remaining board bring-up

Real memory banks and external SDRAM; STM32 RCC/PWR and dual-core boot;
STM32 interrupt routing beyond the existing 32 external lines; GPIO and I2C;
LTDC/DSI and display/touch controller models; SAI/DMA/codec and microphone
adapters; board description and flashable dual-core demo firmware.

Clock pacing currently counts one local tick per retired instruction. It does
not yet model M7 superscalar issue, caches, bus bandwidth or flash wait states.
Raw interpreter throughput is evidence of host headroom, not a guarantee that
audio firmware will match the execution time of a physical H747.

## Primary references

- [ST board user manual UM2411](https://www.st.com/resource/en/user_manual/um2411-discovery-kit-with-stm32h747xi-mcu-stmicroelectronics.pdf)
- [ST dual-core architecture AN5557](https://www.st.com/resource/en/application_note/dm00733995-stm32h745755-and-stm32h747757-lines-dualcore-architecture-stmicroelectronics.pdf)
- [ST board support package](https://github.com/STMicroelectronics/stm32h747i-disco-bsp)
- [ST LCD driver](https://github.com/STMicroelectronics/stm32h747i-disco-bsp/blob/main/stm32h747i_discovery_lcd.c)
- [ST touch interface](https://github.com/STMicroelectronics/stm32h747i-disco-bsp/blob/main/stm32h747i_discovery_ts.h)
- [ST SDRAM interface](https://github.com/STMicroelectronics/stm32h747i-disco-bsp/blob/main/stm32h747i_discovery_sdram.h)
