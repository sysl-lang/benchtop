# Cortex-M interrupt ranges

The NVIC uses the selected core model's interrupt capacity: 32 for M0/M0+/M1,
240 for M3/M4/M7/M23, and 480 for M33/M35P/M52/M55/M85. These are catalog model
capacities, not a claim that every interrupt input is connected on every MCU.

The banked ISER/ICER, ISPR/ICPR and priority registers support those ranges.
Mainline models also expose IABR; ICTR reports the implemented bank count.
Unimplemented trailing bits read zero and ignore writes. Priority byte/halfword
access retains the existing Mainline behavior. STIR accepts implemented lines.
The full nine-bit IPSR is preserved during nested exception return.

Priority selection, PRIMASK, BASEPRI, FAULTMASK, PRIGROUP, WFI wake, exception
stacking and cross-core delivery work across banks. Pending and active state in
additional banks has summary bits. Disabled high sources are masked before
priority scans; idle execution does not scan hundreds of interrupt lines.
Cross-thread producers publish atomically; each CPU takes its own incoming IRQs
at existing execution boundaries. Instructions gain no per-instruction NVIC work.

The Discovery board maps SAI1 to IRQ87 and DMA2 streams 0..7 to
`[56,57,58,59,60,68,69,70]`. The SAI/DMA adapter publishes source edges and levels
without a display lock. DMA bus errors set peripheral TEIF and the corresponding
interrupt; they do not fault the core that advanced the peripheral.

This does not add TrustZone interrupt targeting, fault escalation/lockup, or
cycle-accurate exception-entry/tail-chaining latency. Other peripheral models
remain limited to their individually documented behavior.

References:

- [ST Cortex-M7 programming manual PM0253](https://www.st.com/resource/en/programming_manual/DM00237416.pdf)
- [Arm Cortex-M33 TRM, NVIC programmers model](https://documentation-service.arm.com/static/5f15c42420b7cf4bc5247f3a)
- [STM32H747 interrupt assignments](https://github.com/STMicroelectronics/cmsis-device-h7/blob/master/Include/stm32h747xx.h)
