# STM32H7 audio peripheral foundation

This checkpoint provides reusable SAI, DMA and DMAMUX models under
`sh/sysl/benchtop/peripheral`. They operate on register offsets and bus callbacks,
with no UI, host audio, hard-coded RAM mapping or Cortex-M dependencies.
They are exposed as `stm32-sai`, `stm32-dma` and `stm32-dmamux` catalog entries
and mapped on `stm32h747-disco`. The display firmware still leaves audio idle;
`examples/sai-dma` is a separate flash-linked Sysl interrupt regression.

SAI models one audio sub-block (A or B), with CR1/CR2/FRCR/SLOTR/IMR/SR/CLRFR/DR,
an eight-word FIFO, data-width masking, active TDM slots, mute, FIFO-level flags,
DMA request thresholds, underrun/overrun and interrupt masking. Master mode uses
a rational kernel-clock divider to emit timestamped slot words independently of
advance chunk size. Slave/internal-synchronous blocks accept explicit slot
clocks; the board adapter connects a block configured for internal sync to its
peer. Both blocks must have matching frame configuration. The caller must
serialize accesses and advance the peripheral before MMIO.

DMA models all eight streams of either DMA1 or DMA2. One request moves one bus
beat, with equal byte/halfword/word widths, address increments, NDTR, half/full
transfer flags, masked IRQ levels, W1C flags, circular reload and double-buffer
target switching. Bus/alignment errors stop a stream and preserve its failing
count. DMAMUX1 models sixteen request selectors. FIFO/burst/width conversion,
peripheral flow control, memory-to-memory DMA, DMAMUX synchronization and request
generators are rejected rather than silently accepted. Bus arbitration and
transfer latency are not simulated; supported transfers complete on request. No codec, PDM, SPDIF/AC97, companding,
mono duplication, bit-edge serialization or host playback/capture is included.

The integration regression configures DMA2 stream 1 through DMAMUX channel 9,
request 87, and SAI1 A's DR address. It streams a circular RAM buffer into timed
48 kHz stereo slots and checks ordering, counts, interrupt flags and absence of
underrun. These are the playback selections in ST's Discovery BSP; capture uses
SAI1 B, DMA2 stream 4, request 88. The board's WM8994 codec and its four-slot audio
frame configuration are subsequent integration work.

Primary references:

- [STM32H747 reference manual RM0399](https://www.st.com/resource/en/reference_manual/rm0399-stm32h745755-and-stm32h747757-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [ST CMSIS device register definitions](https://github.com/STMicroelectronics/cmsis-device-h7/blob/master/Include/stm32h747xx.h)
- [ST SAI HAL clock-divider formulas](https://github.com/STMicroelectronics/stm32h7xx-hal-driver/blob/master/Src/stm32h7xx_hal_sai.c)
- [ST DMA request identifiers](https://github.com/STMicroelectronics/stm32h7xx-hal-driver/blob/master/Inc/stm32h7xx_hal_dma.h)
- [Discovery audio wiring and DMA selections](https://github.com/STMicroelectronics/stm32h747i-disco-bsp/blob/main/stm32h747i_discovery_audio.h)

## Board bus and firmware

The Discovery mapping is SAI1 at `0x40015800`, DMA2 at `0x40020400` and DMAMUX1 at
`0x40020800`. DMA controller identities, all base addresses, kernel clock and
SAI request identifiers come from the description. Both DMA1 and DMA2 can be
mapped simultaneously. Default SAI request identifiers are 87 and 88; kernel
clock is 12.288 MHz. SAI A/B DR supports byte, halfword and word accesses at the
DR address; control registers require aligned word accesses.

The adapter interleaves DMA and SAI slot events, even when advancing a large
interval. Internal synchronous capture follows the peer's slots. RX thresholds
have their real buffering consequences: a full threshold retains seven words
between requests, whereas an empty threshold requests every received word.
DMA uses the emulated atomic RAM/flash bus for reads and RAM writes, with
instruction-cache and exclusive-monitor bookkeeping. Transfers do not call UI
rendering or acquire the display lock. Idle SAI uses an atomic next-deadline
check. Raw input/output queues are bounded; no host audio conversion is done
on a CPU worker.

The [extended NVIC](cortex-m-interrupts.md) delivers SAI and all eight DMA stream
interrupts using board-specified IRQs. Tests cover normal and deterministic CPU
workers, WFI wake on actual DMA2 stream1 IRQ57, IABR active status, single
completion delivery, internal synchronous capture, error IRQs, reset and DMA
memory coherence. Rebuild the Sysl firmware with `./examples/sai-dma/build.sh`.
It initializes its own RAM state and uses a vector table defined in Sysl.
RCC/kernel-clock/pin setup still needs physical-board startup support; this is
a flash-linked emulator regression, not complete Discovery hardware startup.

WM8994 codec control, audible playback/capture and the M7 DSP workload are the
next integration steps. The SAI/DMA models are usable through firmware registers
now; this work does not claim complete audio hardware emulation.
