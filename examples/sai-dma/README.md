# SAI/DMA interrupt regression

Sysl firmware linked for STM32H747 internal flash (`0x08000000`) and AXI SRAM
(`0x24000000`). It defines the vector table in Sysl, programs real SAI1 A,
DMA2 stream1, DMAMUX1 channel9 and NVIC registers, and sleeps in WFI until IRQ57
signals completion. The handler records its IABR active bit and clears DMA flags.
The firmware drains the SAI FIFO, disables playback, records status and sleeps.

```
./examples/sai-dma/build.sh
./benchtop run --board stm32h747-disco examples/sai-dma/demo.elf
```

Status symbols: `dma_probe_status` is 1 on success, 2 on DMA error and 3 on
underrun; `dma_probe_irq_count` should be 1; `dma_probe_active` should contain
bit25 (IRQ57 in NVIC bank1). The sixteen raw samples are observed by the SAI
adapter in integration tests; this regression has no codec or audible output.
The board currently supplies a fixed SAI kernel clock through its description.
Physical-board RCC/pin initialization is not implemented by this regression.
