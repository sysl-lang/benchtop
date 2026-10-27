# STM32H7 audio peripheral foundation

This checkpoint provides reusable SAI, DMA and DMAMUX models under
`sh/sysl/benchtop/peripheral`. They operate on register offsets and bus callbacks,
with no UI, host audio, hard-coded RAM mapping or Cortex-M dependencies.
They are not yet mapped into a runnable board's bus or exposed as HOCON catalog
entries. The existing Discovery demo remains unchanged.

SAI models one audio sub-block (A or B), with CR1/CR2/FRCR/SLOTR/IMR/SR/CLRFR/DR,
an eight-word FIFO, data-width masking, active TDM slots, mute, FIFO-level flags,
DMA request thresholds, underrun/overrun and interrupt masking. Master mode uses
a rational kernel-clock divider to emit timestamped slot words independently of
advance chunk size. Slave/internal-synchronous blocks accept explicit slot
clocks; the board adapter will wire their synchronization. The caller must
serialize accesses and advance the peripheral before MMIO.

DMA models all eight streams of either DMA1 or DMA2. One request moves one bus
beat, with equal byte/halfword/word widths, address increments, NDTR, half/full
transfer flags, masked IRQ levels, W1C flags, circular reload and double-buffer
target switching. Bus/alignment errors stop a stream and preserve its failing
count. DMAMUX1 models sixteen request selectors. FIFO/burst/width conversion,
peripheral flow control, memory-to-memory DMA, DMAMUX synchronization and request
generators are rejected rather than silently accepted. Bus arbitration and
transfer latency await the board adapter. No codec, PDM, SPDIF/AC97, companding,
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

Validation: all 722 repository tests passed, including eleven new peripheral
and audio-pipeline regressions. The host build also passed. No audio firmware
or Discovery bus mapping was changed in this checkpoint.
