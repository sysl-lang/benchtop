# RP2040 and RP2350: on-chip coverage, and the plan to emulate both chips fully

This page records what benchtop models of each block in the two datasheets, how it is tested, and the
order the remaining work should go in. Chapter numbers are those of the current datasheets
([RP2040](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf),
[RP2350](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)), each given with its title,
because the title is what survives a new edition. Paths are under `sh/sysl/benchtop/` unless they
start with a top-level directory.

**Status** is one of:

- **full**: every register and behaviour a program can observe is modelled. A fidelity limit that the
  row names, such as an instant PLL lock, is allowed.
- **partial**: the block is on a running bus, and some of its registers or behaviours are missing.
- **register-stub**: the registers are stored and read back, and nothing happens.
- **missing**: no model on the chip's bus. A reusable model elsewhere in benchtop is noted.

**Sizes** are for one agent: **S** is under half a day, **M** is about a day, and **L** is more than a
day. An L is split before it is handed out (§6).

## 0. Where the two chips stand

- **The RP2350 runs on both pairs of cores.** `chip.Rp2350` (`chip/rp2350.sysl`) decodes 19 blocks and the XIP address space
  and raises 11 interrupt lines. `chip.RiscvBoard` runs two Hazard3 harts on it
  (`boards/pico2-riscv.hocon`), and `chip.ArmBoard` (`chip/arm_board.sysl`) two Cortex-M33s
  (`boards/pico2.hocon`), with the M33s' coprocessors on their ports (`chip/coprocessors.sysl`) and a
  boot ROM of benchtop's own at address 0 (`chip/rom/rom.S`). On the RISC-V board the image is an
  ELF loaded into SRAM. The Arm board also has the Pico 2's 4 MB flash behind XIP_BASE and the XIP
  cache's lines as SRAM, and loads an ELF or a UF2 into SRAM, flash or XIP_SRAM (item 4 below).
  Every other address is refused by name; on the RISC-V board that includes the ROM. The SDK's
  `hello_serial` and `blink` run unmodified on the Arm board as `no_flash` and as flash builds
  (`examples/pico2`, items 1 and 4).
- **The RP2040 does not run at all** (`mcus/rp2040.hocon:4`, `peripheral/rp2040/README.md:9–11`).
  Its RESETS, TIMER and watchdog tick generator are library peripherals (`rp-resets`, `rp-timer`,
  `rp-watchdog`). They run on the `virt*` boards through `virt/rp.sysl`, with clk_ref taken to be the
  core clock because there is no CLOCKS block (`virt/rp.sysl:8–12`). The divider is modelled and is
  reached only from a test bus (`v6m/rp2040_peripheral_tests.sysl`). `virt-sio` (`virt/sio.sysl`) is
  an RP2040-shaped SIO with FIFOs, spinlocks and CPUID for the virtual boards. `rp-sio`, `pl011`,
  `dw-i2c` and `rp-adc` are accepted by the checker and marked `modelled = false`
  (`registry.sysl:301,318,322,324`).
- **Reusable models that are not on either RP bus yet:**
  - the PL022 SPI controller (`peripheral/spi/spi.sysl`, run on the STM32 and virtual boards through
    `virt/spi.sysl`);
  - a virtual ADC (`peripheral/adc/adc.sysl`);
  - a virtual PWM channel (`peripheral/pwm/pwm.sysl`);
  - the I²C device-callback layer (`peripheral/i2c/i2c.sysl`);
  - the STM32 DMA (`peripheral/dma/dma.sysl`). Its request/arbitration split is the shape to copy,
    and its registers are not the RP DMA's.
- **The register shape.** The RP models share `AccessError`, the width, alignment and alias rules,
  and replicated narrow writes (`peripheral/rp2040/access.sysl:13–37`). The RP2350 models reuse
  RESETS, TIMER and the access code from `peripheral/rp2040`. New blocks follow the same shape
  (`docs/peripheral-models.md:50–52`).
- **No `pico-examples` checkout exists on this machine.** `pico-scratch/pico-sdk` (the SDK) is
  present, with its own `test/` programs (`hardware_irq_test`, `hardware_pwm_test`,
  `hardware_sync_spin_lock_test`, `kitchen_sink`, `pico_divider_test`, `pico_float_test`,
  `pico_time_test`, `pico_xip_sram_test`, `pico_sha256_test`, `pico_stdio_test`,
  `pico_stdlib_test`, `pico_sync_test`, `pico_sem_test`, `pico_low_power_test`). The Arm toolchain
  is the `gcc-arm-embedded` cask (`pico-scratch/CLAUDE.md:24–42`). There is no RISC-V GCC for the
  SDK's `rp2350-riscv` platform. benchtop's RISC-V fixtures are built with LLVM
  (`examples/pico2-riscv/build.sh`).

## 1. RP2350

| block | datasheet | status | modelled / not | tested now | est | depends on |
|---|---|---|---|---|---|---|
| Cortex-M33 ×2: integer Thumb-2, DSP, FPv5-SP | 3.7 Cortex-M33 | full | The ISA, DSP and FPv5-SP (`v6m/models.sysl:47`, DESIGN.md §2.9), both cores on `chip.Rp2350` through `chip.ArmBoard`, core 1 launched by the ROM protocol into its VTOR, SP and PC. | `v6m` t2/fp/dsp tests on `virt33`; `chip/arm_tests.sysl` | — | — |
| M33 NVIC, SCB, SysTick | 3.7 / 3.2 Interrupts | partial | The NVIC (52 lines, 4 priority bits), SCB and SysTick are generic (`v6m/except.sysl:8`). The lines the chip raises reach the NVIC through `Processor` (TIMER0/1 alarms drive the SDK's `sleep_ms`). | `v6m/nvic_tests`, `priority_tests`, `chip/arm_tests.sysl` | grows with each block | — |
| M33 MPU (PMSAv8, 8 regions per state) | 3.7 | missing | No MPU on any v6m core (no `RLAR`/`RASR` anywhere in `v6m/`). | — | M | — |
| M33 stack limits (MSPLIM/PSPLIM) | 3.7 | full | Every SP write below the limit is the UsageFault STKOF, recorded in CFSR and taken where SHCSR enables it and it preempts, else HardFault (HFSR.FORCED), else a lockup stop; exception stacking that crosses the limit stacks only above it, leaves SP at it and derives the fault (late arrival or pending); `CCR.STKOFHFNMIGN` at negative priority (`v6m/t2secure.sysl`, item 2). | `t2_except.sysl` stack-limit tests; `examples/pico2/stack_limit.c` in `chip/arm_tests.sysl` | — | — |
| TrustZone, Secure-only minimum (SAU stored, TT*, SecureFault, NS transitions refused) | 3.7, 10 Security | full | Secure reset state; SAU_CTRL/TYPE/RNR/RBAR/RLAR stored (8 regions); `TT`/`TTT`/`TTA`/`TTAT` answer from the SAU (no IDAU, no MPU modelled: IREGION and MREGION never valid); SecureFault's enable, priority, SFSR (W1C) and SFAR, pended through SHCSR; `SG` a NOP; `BXNS`/`BLXNS` to Non-secure, EXC_RETURN with S or DCRS clear, `MRS`/`MSR` of `_NS` registers and the PPB's NS alias stop at `NonSecure`, named; EXC_RETURN with ES clear is UsageFault INVPC (item 2). Not detected: INVTRAN (executing from SAU-Non-secure memory). | `v6m/t2secure_tests.sysl`; `examples/pico2/tt.c` in `chip/arm_tests.sysl` | — | — |
| TrustZone, Non-secure state | 3.7, 10 Security | missing | Both worlds' banked state, IDAU, secure gateways and the NS aliases of PPB and SIO. | — | L | Secure-only |
| M33 coprocessors: GPIOC (CP0), DCP (CP4/5), RCP (CP7) | 3.6 Cortex-M33 coprocessors | partial | `MCR`, `MRC`, `MCRR`, `MRRC`, `CDP` and their `…2` forms reach the board through `Bus.coprocessor`, gated by CPACR, which keeps the fields of the ports the board fills (`v6m/coproc.sysl`). **GPIOC is full**: every instruction of `gpio_coproc.h` is its SIO equivalent (`chip/coprocessors.sysl`). **RCP is full** (item 3): salts, canaries, boolean and integer checks, the sequence counter, the random byte and `rcp_panic`, each core its own state; a failed check stops the core at `Fault` with the check named (`chip/rcp.sysl`). **DCP**: the `RCMP` read `runtime_init` makes only; every other DCP instruction, and any encoding outside a coprocessor's set, is refused by name. | `v6m/coproc_tests.sysl`, `chip/rcp_tests.sysl`, `chip/arm_tests.sysl` (`rcp.elf`, `gpioc.elf`) | M (DCP) | — |
| Hazard3 ×2 (RV32IMAC + Zb*/Zc*/Zcmp, PMP, Xh3pmpm, Xh3power) | 3.8 Hazard3 | full | `riscv/rp2350.sysl:6–22` and `riscv/HAZARD3-COVERAGE.md`. | `riscv/hazard3_*_tests`, `chip/tests.sysl:18` | — | — |
| Hazard3 interrupt controller (Xh3irq) | 3.8 | full | MEIEA, MEIPA, MEINEXT and MEICONTEXT (`riscv/csr.sysl:34–42`). Only the lines `signal` drives ever pend (see "Interrupt routing"). | `riscv/hazard3_tests` | — | — |
| Architecture switching (ARCHSEL, both pairs) | 3.9 Architecture switching | missing | The board fixes the cores (`boards/pico2-riscv.hocon` against `pico2`). | — | S | Arm board, boot path |
| Interrupt routing (52 lines) | 3.2 Interrupts | partial | Wired: TIMER0/1 ×4, SIO_FIFO, SIO_BELL, UART0 and UART1, plus MTIME and the soft IRQ (`chip/rp2350.sysl:325–336`). Every other source is absent with its block. | `chip/tests.sysl:18` | grows with each block | — |
| SIO: CPUID, GPIO (both banks), FIFOs (4-deep), spinlocks, FIFO IRQ | 3.1 SIO | full | `peripheral/rp2350/sio.sysl:6–11,13,39–60`. The E2 erratum option (DESIGN.md §9.3) is not built. | `peripheral/rp2350/tests.sysl:129`, `chip/tests.sysl:94,115` | — | — |
| SIO RISC-V platform timer (MTIME, MTIMECMP, soft IRQ) | 3.1.8 / 3.1 SIO | full | `peripheral/rp2350/sio.sysl:15–16,48–51`. | `rp2350/tests.sysl:129` | — | — |
| SIO doorbells (8, SIO_IRQ_BELL) | 3.1 SIO | full | DOORBELL_OUT_SET/CLR (the other core's DOORBELL_IN) and DOORBELL_IN_SET/CLR (this core's), bits 7:0; SIO_IRQ_BELL (26) per core while any bit is set (`peripheral/rp2350/sio.sysl`, `chip/rp2350.sysl` `signal`; item 15). The Non-secure SIO's SIO_IRQ_BELL_NS is not (Secure-only). | `rp2350/sio_tests.sysl`, `chip/interp_tests.sysl` (`interp.elf`) | — | — |
| SIO interpolators INTERP0/1 (per core) | 3.1 SIO | full | `peripheral/rp2040/interp.sysl`, shared with the RP2040: each core's own pair; shift (rotate here), mask, SIGNED, CROSS_INPUT, CROSS_RESULT, ADD_RAW, FORCE_MSB, BLEND (INTERP0), CLAMP (INTERP1), OVERF flags, PEEK/POP, ACCUMx_ADD, BASE_1AND0 (item 15). | `rp2040/interp_tests.sysl`, `rp2350/sio_tests.sysl`, `chip/interp_tests.sysl` (`interp.elf`) | — | — |
| SIO TMDS encoder | 3.1 SIO | missing | Refused (`sio.sysl:10`). | — | S | HSTX (for a use) |
| BUSCTRL: bus priority, performance counters | 2.1 Bus fabric | missing | Not decoded. | — | S (counters) | DMA/cores for events |
| ACCESSCTRL | 10.6 Access control | full (stored) | Every register stored with the datasheet's reset values; the 0xACCE password (not GPIO_NSMASK0/1), sticky LOCK bits that drop a locked core's writes, FORCE_CORE_NS, CFGRESET. Nothing else on the chip consults it (Secure-only, Q3). | `rp2350/sysctl_tests.sysl`, `chip/startup_tests.sysl` | — | — |
| ROM: core-1 launch protocol | 5.3 / 5 Bootrom | partial | The ROM's FIFO echo loop and launch are emulated without ROM code (`chip/rp2350.sysl:271–287`). Core 1 is released by the emulation, not by the real ROM. | `chip/tests.sysl:115` | — | — |
| ROM: contents and API (`rom_func_lookup`, flash ops, `reset_usb_boot`, `get_sys_info`, OTP access) | 5.4 Bootrom APIs | partial | On the Arm board, benchtop's own ROM (`chip/rom/rom.S`, read-only at 0): the vector table, the magic and the well-known pointers, the Arm lookup, and `bootrom_state_reset` ('SR'), which has no state to reset. Any other code looks up as 0, as the real ROM answers for a code it lacks. On the RISC-V board a ROM read is refused. | `chip/arm_tests.sysl` | L | XIP/QMI, OTP, BOOTRAM |
| Boot path: IMAGE_DEF, partition tables, UF2, secure-boot flags, signature check | 5 Bootrom, 10 Security | missing | Boot is the debugger entry into SRAM (`chip/riscv_board.sysl:13–17`). DESIGN.md §9.2 option 3. | — | L | ROM, OTP, XIP, SHA-256 |
| SRAM 0–9 (520 KB) | 4.2 SRAM | full | `mcus/rp2350.hocon:19`. Striping does not matter to a program. | every chip test | — | — |
| BOOTRAM (with boot locks) | 4.3 Boot RAM | full | 1 KB of SRAM, WRITE_ONCE0/1 and the eight boot locks with BOOTLOCK_STAT (`peripheral/rp2350/startup.sysl`). | `peripheral/rp2350/startup_tests.sysl` | — | — |
| XIP: flash window, aliases, cache, cache-as-SRAM, XIP_CTRL, streaming FIFO | 4.4 XIP | full | One 4 MB flash array (`parts/qspi_flash`, a W25Q32RV) behind the cached, uncached and untranslated windows, read and executed in place on the cores' fast path from the cached window's first 4 MB (`chip/arm_board.sysl`); the rest through the chip, translated by ATRANS and wrapping at the device's size (`peripheral/rp2350/xip.sysl`). XIP_CTRL in full, the cache's tags with every maintenance operation, lines pinned over XIP_SRAM holding their bytes (a fully pinned page on the fast path, a partly pinned one checked line by line), the streaming FIFO and XIP_AUX. Fidelity limits: the cache is functional, not timed (no lookups, so CTR_HIT/CTR_ACC are refused by name and a read is never stale); a pin over flash, a write through XIP with WRITABLE_M0 set, and code fetched from flash in direct mode or from an unpinned XIP_SRAM line are the cases not run or not refused (item 4 below). | `peripheral/rp2350/xip_tests.sysl`, `chip/xip_tests.sysl` | — | — |
| QMI (flash and PSRAM interface; direct mode) | 12.14 QMI | full | Direct mode with both FIFOs, NOPUSH, 16-bit entries, ASSERT and AUTO chip selects, RX-full stall and BUSY, all to the flash on CS0; the window, timing and ATRANS registers stored. Instantaneous: no clock divider timing. CS1 (no PSRAM on the Pico 2) and a non-identity ATRANS0-3 are refused by name. The ROM's flash routines (`flash_range_erase` and friends) need item 6. | `peripheral/rp2350/xip_tests.sysl`, `chip/xip_tests.sysl` (`qmi_flash`) | — | — |
| OTP | 13 OTP | missing | Not decoded. It is read for the unique ID and for boot configuration. | — | M (stored array, ROM access); L (ECC, locks, keys) | ROM |
| POWMAN, including the AON timer, LPOSC, VREG and BOD control | 6 Power | partial | The store is done: VREG (locked until UNLOCK, VSEL limit), BOD, LPOSC and XOSC settings, SEQ_CFG, CHIP_RESET (HAD_POR), WDSEL, SCRATCH/BOOT, the interrupt registers into IRQs 44 and 45, all behind the 0x5AFE password. Refused by name, for item 27: STATE, the TIMER/time/alarm registers, PWRUP0-3, CURRENT_PWRUP_REQ, LAST_SWCORE_PWRUP, BOOTDIS. LPOSC is still a constant 32768 Hz. | `rp2350/sysctl_tests.sysl`, `chip/startup_tests.sysl` | M (AON timer) | TICKS/clocks |
| GLITCH_DETECTOR | 10.9 Glitch detector | missing | Not decoded. | — | S (store) | — |
| RESETS (29 outputs) | 7.5 Subsystem resets | full | `peripheral/rp2350/timing.sysl:15–22` over `rp2040/resets.sysl:7–11`. Release completes at once. | `rp2350/tests.sysl:15`, `chip/tests.sysl:43,81` | — | — |
| PSM (power-on state machine; FRCE_ON/FRCE_OFF/DONE) | 7.4 Power-on state machine | full | The 25-stage sequence: FRCE_OFF holds a stage and those after it, FRCE_ON releases from a later stage, DONE is computed, WDSEL stored. PROC1 forced off stops core 1 and its release re-enters the ROM wait with a 0 for core 0 (`multicore_reset_core1`). Forcing any other stage off is refused by name. | `rp2350/sysctl_tests.sysl`, `chip/startup_tests.sysl` | — | — |
| CLOCKS | 8.1 Clocks | partial | CTRL and DIV are stored. clk_ref and clk_sys follow their muxes and drive time. SELECTED is one-hot (`peripheral/rp2350/clocks.sysl:25–30`). The frequency counter FC0 is run (it measures the modelled tree, finishing at once); resus is stored. clk_peri, clk_usb, clk_adc and clk_hstx have frequencies from their AUXSRC and DIV, which FC0 reads, but no consumer. GPOUT is not driven on a pin. | `rp2350/tests.sysl:112`, `rp2350/sysctl_tests.sysl`, `chip/startup_tests.sysl` | S | consumers |
| XOSC | 8.2 Crystal oscillator | partial | STABLE after STARTUP.DELAY×256 (`clocks.sysl:97–105`). DORMANT and COUNT are not modelled. | `rp2350/tests.sysl:81` | S | — |
| ROSC (including RANDOMBIT) | 8.3 Ring oscillator | full | Every register, the code-protected fields with BADWRITE, DORMANT, COUNT. Frequency stays the nominal 11 MHz (Q8) and follows ENABLE and DORMANT. RANDOMBIT is a seeded LFSR (seed 0x3F04B16D at power-on, reloaded by RANDOM, one step per read). | `rp2350/sysctl_tests.sysl`, `chip/startup_tests.sysl` | — | — |
| TICKS (6 generators) | 8.4 Tick generators | full | `peripheral/rp2350/timing.sysl:24–90`. | `rp2350/tests.sysl:25` | — | — |
| PLL_SYS, PLL_USB | 8.6 PLL | full | LOCK once powered with an FBDIV. The output frequency is computed (`clocks.sysl:167–225`). Lock is instant. | `rp2350/tests.sysl:81` | — | — |
| IO_BANK0: FUNCSEL, overrides, STATUS | 9 GPIO | partial | 48 GPIOs with overrides (`peripheral/rp2350/gpio.sysl:7–80`). Only SIO is a real driver. UART TX is "idles high" on `n % 4 == 0` (`chip/rp2350.sysl:299–303`), and no peripheral's bit stream reaches a pin. There is no mux layer for PIO, PWM, SPI or I²C (`docs/peripheral-models.md:17–22`). | `chip/tests.sysl:94`, `rp2350/tests.sysl:183` | M (mux layer) | each peripheral |
| GPIO interrupts (PROC0/1 INTE/INTF/INTS, edge latch, IO_IRQ_BANK0, dormant wake) | 9 GPIO | missing | Refused (`gpio.sysl:9–10`). | — | M | IRQ routing |
| PADS_BANK0 (ISO, IE, OD, pulls, drive, slew) | 9.8 Pads | full | `gpio.sysl:82–131`. Drive and slew are stored. | `rp2350/tests.sysl:169` | — | — |
| IO_QSPI, PADS_QSPI (including the BOOTSEL read via QSPI_SS) | 9 GPIO | missing | Not decoded. | — | S | XIP for the shared pins |
| UART0/1 (PL011) | 12.1 UART | partial | DR both ways, FR, the divisors, LCR_H/CR stored, 32-entry FIFOs, RX/TX interrupts (`peripheral/rp2350/uart.sysl:6–12`). Not modelled: byte timing from clk_peri and the baud, modem lines, break and errors, FIFO-level interrupts beyond RX/TX, RT timeout, DMA DREQs (DMACR is stored), and the bit stream on the pin. Atomic aliases of DR and ICR are refused (`uart.sysl:84,112`). | `rp2350/tests.sysl:196`, `chip/tests.sysl:18` | M | CLOCKS (clk_peri), DMA |
| SPI0/1 (PL022) | 12.3 SPI | missing | Not on the chip's bus. The PL022 model exists (`peripheral/spi/spi.sysl`) and runs on STM32 and virtual boards. | `peripheral/spi/tests.sysl` (6) | S (place) + S (DREQ) | mux, DMA |
| I2C0/1 (DW_apb_i2c) | 12.2 I2C | missing | `dw-i2c` has `modelled = false` (`registry.sysl:322`). The device callbacks exist (`peripheral/i2c/i2c.sysl`). | — | M (master), S (slave) | mux, DMA |
| PWM (12 slices) | 12.5 PWM | missing | Only `virt-pwm` (`peripheral/pwm/pwm.sysl`). | — | M | mux, DMA, IRQ |
| ADC and temperature sensor | 12.4 ADC | missing | `rp-adc` has `modelled = false` (`registry.sysl:324`). The analogue input path exists in `peripheral/adc/adc.sysl`. | — | M | clk_adc, DMA |
| DMA: 16 channels, DREQs, pacing timers, chaining, ring, IRQ0–3, sniff/CRC, MPU/secure attributes | 12.6 DMA | missing | Not decoded. | — | L (core) + S (sniff/CRC) + S (security attrs) | DREQs from every peripheral |
| PIO0/1/2 | 11 PIO | missing | Not decoded. DESIGN.md §9.7 costs it at 4–6 agent-days. The RP2350 adds a third block, `MOV` to and from the RX FIFO, and the GPIO base (16/48). | — | L (machine) + M (GPIO/DMA/IRQ) | mux, DMA, IRQ, clk_sys |
| USB controller, device mode (and the CDC bridge to the host) | 12.7 USB | register-stub | SIE_CTRL alone is stored, which `runtime_init_usb_power_down` reads and sets; it is not gated by RESETS, since the SDK reads it before releasing USBCTRL (`peripheral/rp2350/startup.sysl`). Every other register is refused. DESIGN.md §7 "Later" costs the controller. | `peripheral/rp2350/startup_tests.sysl` | L + M (bridge) | clk_usb, PLL_USB, IRQ |
| USB controller, host mode | 12.7 USB | missing | — | — | L | device mode |
| TIMER0/1 | 12.8 System timers | full | 64-bit counter, latch, 4 alarms, LOCKED, SOURCE (`peripheral/rp2350/timing.sysl:91–145`). | `rp2350/tests.sysl:44`, `chip/tests.sysl:18` | — | — |
| WATCHDOG | 12.9 Watchdog | partial | CTRL and SCRATCH0–7 are stored. The counter does not count, TRIGGER makes no reset, and REASON reads 0 (`timing.sysl:147–186`). | `rp2350/tests.sysl:70` | M | chip reset path, PSM |
| HSTX | 12.11 HSTX | missing | Not decoded. | — | M | clk_hstx, DMA, mux, a display part |
| TRNG | 12.12 TRNG | missing | Not decoded. | — | S | — |
| SHA-256 | 12.13 SHA-256 | missing | Not decoded. | — | S | DMA (for the SDK's DMA path) |
| SYSINFO (CHIP_ID, PLATFORM, GITREF) | 12.15 System control | full | CHIP_ID 0x30004927, PACKAGE_SEL 1 (RP2350A), PLATFORM ASIC, GITREF 0; read-only. | `rp2350/sysctl_tests.sysl`, `chip/startup_tests.sysl` | — | — |
| SYSCFG (NMI masks, proc in-sync bypass, DBGFORCE, mem power-down) | 12.15 System control | full (stored) | PROC_CONFIG (halted flags read 0), PROC_IN_SYNC_BYPASS(_HI), DBGFORCE, MEMPOWERDOWN (memory is not lost), AUXCTRL stored with their writable bits. | `rp2350/sysctl_tests.sysl` | — | — |
| TBMAN | 12.15 / 2 System bus | missing | Not decoded. | — | S | — |
| Debug: SW-DP, RESCUE, CoreSight trace | 3.5 Debug | missing | Debugger *entry* is how images boot. The probe itself is not modelled. | — | L | — |

## 2. RP2040

| block | datasheet | status | modelled / not | tested now | est | depends on |
|---|---|---|---|---|---|---|
| Chip layer: bus decode, clock tree, IRQ routing, Pico board | 2.2 Address map | missing | `mcus/rp2040.hocon:4` says "does not run". There is no `chip.Rp2040`. | — | M | — |
| Cortex-M0+ ×2 | 2.4 Cortex-M0+ | partial | v6m, run on the `virt*` boards (DESIGN.md §2.8). The 8-region MPU (`mcus/rp2040.hocon:9`) is not modelled. | `v6m/*_tests` | S (MPU, PMSAv7 subset) | — |
| NVIC and SysTick (M0+) | 2.3.2 Interrupts, 2.4 | full | `v6m/except.sysl`, `v6m/nvic.sysl`. 26 IRQ lines once a chip routes them. | `v6m/nvic_tests` | — | chip layer |
| SIO: CPUID, FIFOs (8-deep), spinlocks, FIFO IRQ | 2.3.1 SIO | partial | `virt-sio` (`virt/sio.sysl:3–40`) for the virtual boards. It has no GPIO registers, and `rp-sio` is `modelled = false` (`registry.sysl:301`). The RP2350 SIO model is the base to parameterise (depth 8, no high bank, no MTIME). | `virt/session_tests`, `virt/machine_tests` | S | chip layer |
| SIO GPIO (OUT/OE/IN, SET/CLR/XOR) | 2.3.1 SIO | missing | Not on an RP2040 bus. Shared with the RP2350 model. | — | S | IO_BANK0 |
| SIO hardware divider (per core) | 2.3.1.5 Integer divider | full | `peripheral/rp2040/divider.sysl:5–84`: 8-cycle completion, DIRTY, signed and unsigned. Reached only from a test bus. | `rp2040/tests.sysl`, `v6m/rp2040_peripheral_tests.sysl` | S (wire) | chip layer |
| SIO interpolators (with the RP2040's blend and clamp modes) | 2.3.1.6 Interpolator | full | `peripheral/rp2040/interp.sysl`, the RP2350's model with SHIFT a logical right shift (item 15). Reached only from tests until the RP2040 chip layer places it. | `rp2040/interp_tests.sysl` (both shifts) | S (wire) | chip layer |
| Bus fabric, BUSCTRL, performance counters | 2.1 Bus fabric | missing | — | — | S | — |
| DMA: 12 channels, DREQs, pacing timers, chaining, ring, sniff/CRC | 2.5 DMA | missing | — | — | L (shared with RP2350, which has more channels and security) | DREQs |
| ROM: contents, float/double/mem tables, `rom_func_lookup`, core-1 launch | 2.8 Bootrom | missing | Not mapped. The RP2040 SDK calls the ROM for float, double, memcpy and memset (DESIGN.md §10.1). | — | L | XIP/SSI for the flash routines |
| Boot sequence (boot2, CRC check, USB boot) | 2.7 Boot sequence | missing | DESIGN.md §10.1: the debugger entry, boot2 not run. | — | M (debugger entry); L (real path) | ROM, SSI |
| SRAM (264 KB, striped, plus non-striped aliases at 0x2100_0000) | 2.6.2 SRAM | partial | The 264 KB region is described (`mcus/rp2040.hocon:14`). The aliases are absent, and the chip does not run. | — | S | chip layer |
| XIP, XIP cache, XIP_CTRL, streaming | 2.6.3 Flash | missing | — | — | M (shared with RP2350 XIP) | loader |
| SSI (DW_apb_ssi) | 4.10 SSI | missing | — | — | S (store, flash reads work) / M (direct commands) | XIP |
| VREG_AND_CHIP_RESET | 2.10 Core supply regulator, 2.12 Chip-level reset | missing | — | — | S | — |
| PSM | 2.13 Power-on state machine | missing | — | — | S (shared) | — |
| RESETS (25 outputs) | 2.14 Subsystem resets | full | `peripheral/rp2040/resets.sysl:7–55` on the `virt*` boards (`virt/rp.sysl:19–24`). | `rp2040/tests.sysl`, `virt/rp_tests.sysl:246,268` | — | — |
| CLOCKS (no TICKS; clk_rtc instead of clk_hstx) | 2.15 Clocks | missing | `virt/rp.sysl:8–12`: "benchtop models no CLOCKS block". The RP2350 model is the base. | — | S | — |
| XOSC | 2.16 Crystal oscillator | missing | Shared with the RP2350. | — | S | — |
| ROSC (RANDOMBIT) | 2.17 Ring oscillator | missing | Shared. | — | S | — |
| PLL_SYS, PLL_USB | 2.18 PLL | missing | Shared with the RP2350 (same registers). | — | S | — |
| IO_BANK0, PADS_BANK0 (30 GPIOs, no ISO bit) | 2.19 GPIO | missing | The RP2350 model is the base (DESIGN.md §10.1). | — | S | chip layer |
| IO_QSPI, PADS_QSPI | 2.19 GPIO | missing | — | — | S | — |
| GPIO interrupts (PROC0/1, DORMANT_WAKE) | 2.19 GPIO | missing | Shared with the RP2350 item. | — | S (after RP2350) | — |
| SYSINFO | 2.20 Sysinfo | missing | `rp2040_chip_version()` and `running_on_fpga()` read it. | — | S | — |
| SYSCFG | 2.21 Syscfg | missing | — | — | S | — |
| TBMAN | 2.22 TBMAN | missing | — | — | S | — |
| PIO0/1 | 3 PIO | missing | The same machine as the RP2350's, with two blocks and no RP2350 additions. | — | (shared) | mux, DMA |
| USB controller (device and host) | 4.1 USB | missing | The same controller as the RP2350's. | — | (shared) | — |
| UART0/1 (PL011) | 4.2 UART | missing | `pl011` is `modelled = false` (`registry.sysl:318`). The RP2350 model is reusable (`peripheral/rp2350/uart.sysl`). | — | S (place) | chip layer |
| I2C0/1 | 4.3 I2C | missing | Shared. | — | (shared) | — |
| SPI0/1 (PL022) | 4.4 SPI | missing | The model exists (`peripheral/spi/spi.sysl`). | — | S (place) | chip layer |
| PWM (8 slices) | 4.5 PWM | missing | Shared. | — | (shared) | — |
| TIMER | 4.6 Timer | full | `peripheral/rp2040/timer.sysl:5–120`: latch, 4 alarms, pause, debug pause. | `rp2040/tests.sysl`, `virt/rp_tests.sysl` (13) | — | — |
| WATCHDOG | 4.7 Watchdog | partial | The tick generator and SCRATCH are modelled. CTRL, LOAD and REASON are refused as unmapped (`peripheral/rp2040/watchdog.sysl:47–49`). | `virt/rp_tests.sysl:304` | M (shared with RP2350) | chip reset path |
| RTC | 4.8 RTC | missing | — | — | M | clk_rtc |
| ADC and temperature sensor | 4.9 ADC | missing | `rp-adc` has `modelled = false`. | — | (shared) | — |

## 3. Counts

| chip | full | partial | register-stub | missing | rows |
|---|---|---|---|---|---|
| RP2350 | 18 | 10 | 1 | 28 | 57 |
| RP2040 | 5 | 4 | 0 | 28 | 37 |

One RP2350 block is a register-stub today (its row says which). Every block that is not modelled is refused by the bus, which is
benchtop's policy for unsupported accesses (`docs/peripheral-models.md:54`). Several items below
build register-stubs on purpose: a block stored as written, with a named refusal for any behaviour
the stub does not carry out.

## 4. Priority, with the reasons

The order follows what an unmodified Pico SDK program touches, in order, from reset. For the Pico 2,
the binary in question is an **Arm Secure image linked to flash**: that is what `pico2` builds and what
every program in the org is.

1. **Making SDK binaries run at all.** None of these is a "peripheral", and every SDK program needs
   all of them before its first line of `main`:
   - the M33s on `chip.Rp2350`;
   - the Secure-only TrustZone minimum, because `crt0.S` writes MSPLIM and the image is Secure;
   - the RCP and GPIO coprocessors, because `crt0.S` enables the RCP and `gpio_put` uses CP0;
   - XIP flash, because the default binary type runs from flash;
   - the boot ROM's function table, because the SDK looks functions up through it.

   The DCP follows as soon as a program prints a `%f`.
2. **The blocks `runtime_init` polls.** RESETS, TICKS, PLLs and TIMER are done. PSM (core-1 reset),
   ROSC, BOOTRAM (boot locks), SYSINFO, SYSCFG, the POWMAN/VREG store and FC0 are not. Each one
   missing is a refusal at start-up. The census in item 1 below lists them from a real run instead of
   from memory.
3. **UART/stdio.** `hello_serial` and every `printf` go through UART0. The model works, but bytes leave
   at once instead of at the baud rate, and there is no DREQ.
4. **GPIO and IRQ.** GPIO interrupts and the function mux. Every button example needs the
   interrupts. The mux is what every later peripheral drives pins through, so it comes before them.
5. **Timers and watchdog.** The timers are done. The watchdog's counter and the chip reset it makes
   are what `hello_watchdog` and the SDK's `watchdog_reboot` (used by `reset_usb_boot` fallbacks and
   by the REPLs) need.
6. **DMA.** Its DREQs are what UART, SPI, PWM, ADC, PIO and HSTX feed. It is built before PIO, so PIO
   is built against a real DREQ.
7. **PIO.** Large. `ws2812`, many pico-examples, and the Pico W's CYW43 gSPI all need it, so it
   gates the `pico2w` board and the LED of the user's own board.
8. **USB device mode and the CDC bridge.** Large. Most org programs use `stdio_usb`
   (DESIGN.md §9.8), so this is what makes them run unmodified.
9. **The RP2040 chip layer.** Built over the RP2350 models once items 1–6 have made them
   chip-agnostic. The RP2040's own additions are the ROM's float and double tables (which the SDK
   calls constantly), the divider wiring, SSI, RTC and VREG_AND_CHIP_RESET.
10. **The rest, by consumer:**
    - SPI, I²C, PWM, ADC and temperature (picokit and the breadboard kit);
    - interpolators and doorbells;
    - TRNG and SHA-256;
    - OTP, then the real boot path;
    - POWMAN's AON timer and the RP2040 RTC;
    - HSTX, then BUSCTRL counters;
    - the remaining stubs.

## 5. Test approach for each block

**The oracles are unmodified programs**, built with `pico-scratch/pico-sdk` for `PICO_BOARD=pico2`
(and `pico` for the RP2040), and compared against the user's real board (DESIGN.md §9.8). They are
judged by output: UART bytes, GPIO levels over time, and a register dump. `pico-examples` must be
cloned first (into `pico-scratch/pico-examples`, at the tag matching the SDK). The example names below
are the pico-examples tree's, and each item verifies its names against that checkout. Every oracle
run also lands as a sysl test that loads the built ELF from `testdata/` and asserts the bytes and
levels (Rule 4a), beside a register-level test of the model. That is the pattern of
`chip/tests.sysl:18` and `peripheral/rp2350/tests.sysl`.

| block | oracle programs | sysl-side targeted tests |
|---|---|---|
| Arm board, M33 on chip | a hand-written M33 twin of `examples/pico2-riscv/blink.c`; then `blink`, `hello_serial` | the board boots an ELF, core 1 launches, a refused block stops at the PC (as `chip/tests.sysl:18–54` does for RISC-V) |
| Secure-only TrustZone, STKOF | SDK `kitchen_sink`; `crt0.S` itself | TT answers, SG/BXNS stop with "Non-secure state is not modelled", STKOF UsageFault versus HardFault priority (`CCR.STKOFHFNMIGN`) |
| RCP, GPIOC, DCP | `hello_serial` (crt0's RCP), `blink` (CP0), SDK `pico_float_test` and a `printf("%f")` program (DCP) | each coprocessor op against the datasheet's description; a failed RCP check stops with the check named; CPACR gating |
| XIP and QMI | SDK `pico_xip_sram_test`; `flash/xip_stream`, `flash/cache_perfctr`, `flash/program` | aliases read one image; cache-as-SRAM when disabled; the maintenance alias; the loader writes a UF2 and an ELF's PT_LOAD into flash |
| Boot ROM API | `system/unique_board_id`, `flash/program`, `reset/hello_reset`; every program's `rom_func_lookup` | each table entry looked up by code; each implemented function against its ROM documentation; an unimplemented one refused by name |
| PSM, ROSC, BOOTRAM, SYSINFO, SYSCFG, POWMAN store, FC0 | `hello_serial` reaches `main`; `clocks/hello_48MHz`, `clocks/hello_gpout`, `system/rand` | register-level per block; FC0 measures each clock the tree produces |
| UART | `hello_serial`, `uart/hello_uart`, `uart/uart_advanced` (IRQ RX) | byte timing at the programmed baud from clk_peri; the RT timeout; FIFO-level IRQs; break and framing errors injected from the host side; DREQ levels |
| GPIO interrupts and the mux | `gpio/hello_gpio_irq`, `blink` with a button part | edge and level latching, per-core INTE/INTF/INTS, IO_IRQ_BANK0 into both cores; each FUNCSEL routes its producer, and an unrouted producer drives nothing |
| Watchdog | `watchdog/hello_watchdog` | the countdown at the TICKS rate; TRIGGER and expiry reset the chip; SCRATCH survives; REASON reads the cause |
| Doorbells, interpolators | `interp/hello_interp`, SDK `kitchen_sink`; a two-core doorbell program | interpolator lanes against the datasheet's worked examples (blend, clamp, signed); doorbell set/clear and IRQ on the other core |
| DMA | `dma/hello_dma`, `dma/control_blocks`, `dma/channel_irq`, `dma/sniff_crc`, `adc/dma_capture`, `spi/spi_dma` | chaining, ring, TREQ pacing timers, the 4 IRQ lines, abort; CRC-32, CRC-16 and sum checked against standard vectors |
| PIO | `pio/hello_pio`, `pio/pio_blink`, `pio/ws2812`, `pio/uart_tx`, `pio/uart_rx`, `pio/spi`, `pio/i2c`, `pio/logic_analyser`, `pio/squarewave` | each instruction against the datasheet's semantics; autopush and autopull thresholds; side-set; FIFO join; the clock divider; WAIT on pin and IRQ; DREQs; the RP2350's additions |
| USB device | `hello_world/usb`, `usb/device/dev_lowlevel`, `usb/device/dev_hid_composite` | enumeration by benchtop's own host; CDC bytes to a pty; endpoint buffer control |
| SPI, I²C, PWM, ADC | `spi/spi_master_slave`, `spi/bme280_spi`, `i2c/bus_scan`, `i2c/slave_mem_i2c`, `pwm/hello_pwm`, `pwm/measure_duty_cycle`, `adc/hello_adc`, `adc/onboard_temperature`; picokit | register-level per model; pins through the mux to parts on the workbench |
| TRNG, SHA-256, OTP | `system/rand`, `sha/sha256`, `otp/hello_otp`; SDK `pico_sha256_test` | SHA-256 against the FIPS 180-4 vectors; OTP row reads, ECC and locks |
| POWMAN AON timer, RTC (RP2040) | `rtc/hello_rtc` and `rtc/rtc_alarm` (RP2040); the SDK's `pico_aon_timer` on the RP2350 | alarm at the second it names; LPOSC-driven counting |
| RP2040 chip layer and ROM | `blink`, `hello_serial`, `divider/hello_divider`, `multicore/hello_multicore`, SDK `pico_divider_test`, `pico_float_test` | ROM float and double functions against host IEEE results; the divider through the bus; FIFO depth 8 |
| HSTX | `hstx/dvi_out_hstx_encoder` | the shift-register and TMDS output against the datasheet's examples |

## 6. Cut lines — DECIDED

Each of these is a block, or part of one, that could reasonably be left out. All fourteen are decided:

| | decision |
|---|---|
| Q1 | the **real** boot ROM images, with OTP, BOOTRAM, QMI direct mode and the RCP modelled for them |
| Q2 | the real boot path is built through **IMAGE_DEF**; the debugger entry stays as well |
| Q3 | **Secure-only**; Non-secure state is not built |
| Q4 | USB **device** mode only, with the CDC bridge |
| Q5 | the **GDB stub**; no SW-DP, RESCUE or CoreSight trace |
| Q6 | GLITCH_DETECTOR and TBMAN are **register stubs** |
| Q7 | DORMANT and SLEEP are a **time skip** to the next wake event; POWMAN domains are stores |
| Q8 | analogue stays **ideal and deterministic**; baud rates and PIO timing are **exact** |
| Q9 | HSTX **later**, once a display part can show it |
| Q10 | OTP **with ECC** and page locks |
| Q11 | the RP2350B board **later**; the Pico 2 (RP2350A) now |
| Q12 | only the errata the **SDK checks for** (RP2350-E2 behind an option) |
| Q13 | **Arm first**, then RISC-V |
| Q14 | the RP2040's chip layer comes **right after the RP2350's start-up items**, so each later peripheral is tested on both chips |

The questions as they were asked:

- **Q1. Real boot ROM, or a high-level ROM?** Mapping the real ROM images (`pico-bootrom-rp2040`,
  `pico-bootrom-rp2350`) is faithful. It needs OTP, BOOTRAM, QMI direct mode and the RCP all
  modelled before any SDK program runs. A ROM written in benchtop that answers `rom_func_lookup` with
  its own functions runs sooner and diverges where a program relies on the ROM's exact behaviour.
  Which one?
- **Q2. The real boot path** (IMAGE_DEF, partition tables, signature and encrypted boot, the secure-boot
  OTP flags). Should it be built, or should the debugger entry stay the only boot?
- **Q3. TrustZone Non-secure state.** Should only the Secure-only minimum be built?
- **Q4. USB host mode.** Should only device mode (with the CDC bridge) be built?
- **Q5. Debug: SW-DP, RESCUE, CoreSight trace.** Leave these out in favour of the GDB stub (DESIGN.md
  §7)?
- **Q6. GLITCH_DETECTOR and TBMAN.** Register-stubs only, or out entirely?
- **Q7. Low-power behaviour**: DORMANT, SLEEP, POWMAN power domains, memory power-down. Should these
  be stores, or modelled to the point that `pico_low_power_test` and the sleep examples run?
- **Q8. Analogue and timing fidelity**: ADC noise and INL, the temperature sensor's curve, the ROSC
  frequency spread, XOSC and PLL lock delays, and bus-contention timing in BUSCTRL. Should these stay
  ideal and deterministic, as `peripheral/adc` is?
- **Q9. HSTX.** Should it be built, given that it needs a display part to be seen?
- **Q10. OTP beyond a stored array**: ECC, page locks and key storage. Are these needed?
- **Q11. The RP2350B** (48 GPIOs, QFN-80). The models already carry the high bank. Should a board for
  it be added, or should only the RP2350A (the Pico 2) be built?
- **Q12. Errata emulation** (RP2350-E2 behind an option, DESIGN.md §9.3, and others). Is this wanted?
- **Q13. Order: Arm Pico 2 first, or RISC-V first?** The RISC-V cores already run on the chip. The SDK's
  `rp2350-riscv` platform needs a RISC-V GCC that is not installed. The org's programs are Arm. This
  plan puts Arm first.
- **Q14. RP2040 timing.** Should the RP2040 wait until the RP2350's peripherals are complete, or should
  its chip layer come right after the RP2350's start-up items, so that each later peripheral is
  tested on both chips as it lands?

## 7. Proposed agent items, in dependency order

Each item is one agent: one branch, a targeted gate, and a READY report. An "after" names the items
that must land first. Items with no shared "after" run in parallel.

| # | item | size | after |
|---|---|---|---|
| 1 | **Arm Pico 2 board**: an `ArmBoard` beside `RiscvBoard` running two M33s on `chip.Rp2350`. It covers ELF-to-SRAM debugger entry, the core-1 ROM launch into VTOR, SP and PC, and the 52 IRQ lines into the NVIC through `Processor`. A hand-written M33 twin of `examples/pico2-riscv/blink` is its test. It also clones and builds `pico-examples`, and records the refusal census of `hello_serial` and `blink` (`no_flash` builds) in its report. | M | — |
| 2 | **M33 Secure-only security minimum** (DESIGN.md §2.7): Secure reset state, SAU stored, `TT*`, SecureFault with SFSR/SFAR, NS transitions stopped by name, and STKOF taken as a UsageFault. **Done** (below). | M | — |
| 3 | **RCP (CP7) and GPIO coprocessor (CP0)** on a coprocessor port the chip supplies. **Done** (below). | M | 1 |
| 4 | **XIP flash and QMI**: one flash image behind the 0x1000_0000 aliases, the cache, cache-as-SRAM, XIP_CTRL, QMI stored with direct mode, and the loader writing an ELF's PT_LOAD and a UF2 into flash. `pico_xip_sram_test` is its oracle. **Done** (below). | M | 1 |
| 5 | **Start-up register blocks** (done, see "Item 5, done"): PSM (FRCE_OFF and DONE), ROSC with RANDOMBIT, BOOTRAM, SYSINFO, SYSCFG, the POWMAN/VREG store, ACCESSCTRL stored, and CLOCKS FC0 measuring the tree. Every refusal in item 1's census is either answered here or named. | M | 1 |
| 6 | **Boot ROM function table**: the shape follows Q1; with an HLE ROM, the lookup table plus the functions `runtime_init` and stdio reach. It is split in two if Q1 chooses the real ROM. | L | 4, 5 |
| 7 | **DCP (CP4/5)**: the double coprocessor. `pico_float_test` and a `printf("%f")` program are its oracles. | M | 3 |
| 8 | **UART completion**: baud-timed bytes from clk_peri, both UARTs' pins correct, RT timeout, FIFO-level IRQs, break and errors, DREQ outputs. | M | 6 |
| 9 | **GPIO function mux and GPIO interrupts**: one routing layer where every producer offers level and OE, IO_IRQ_BANK0, and per-core INTE/INTF/INTS. `gpio/hello_gpio_irq` is its oracle. | M | 6 |
| 10 | **Watchdog counter and chip reset path**: countdown, TRIGGER, REASON, SCRATCH surviving, and the machine restarting through the boot path. | M | 5 |
| 11 | **DMA core**: channels, DREQ inputs, pacing timers, chaining, ring, IRQs. The register model is shared by both chips' channel counts. | L | 9 |
| 12 | **DMA sniff/CRC and security attributes**. | S | 11 |
| 13 | **PIO state machine**: the instruction set, FIFOs, shift registers, side-set, divider, IRQ flags, and the RP2350 additions, with register-level tests. | L | 9 |
| 14 | **PIO integration**: pins through the mux, DREQs, the IRQ lines. `ws2812`, `pio_blink`, `uart_tx` and `uart_rx` are its oracles. | M | 11, 13 |
| 15 | **Doorbells and interpolators** (shared by both chips). **Done** (below). | M | 1 |
| 16 | **SPI on the chip**: the PL022 placed, with DREQs. | S | 9, 11 |
| 17 | **I²C (DW_apb_i2c) master and slave**. | M | 9, 11 |
| 18 | **PWM** (12 slices, IRQ, DREQ, pins through the mux). | M | 9, 11 |
| 19 | **ADC and temperature sensor** (round robin, FIFO, DREQ). | M | 11 |
| 20 | **USB device controller**: endpoints, buffers, IRQ. | L | 6 |
| 21 | **USB host enumerator and CDC bridge to a pty**. `hello_world/usb` is its oracle. | M | 20 |
| 22 | **RP2040 chip layer**: `chip.Rp2040` over the shared models (CLOCKS without TICKS, pads without ISO, SIO with 8-deep FIFOs and the divider wired), and the `pico` board running. | M | 5, 8, 9 |
| 23 | **RP2040 ROM**, following Q1: the float, double and mem tables plus the lookup. | L | 22 |
| 24 | **RP2040 SSI, VREG_AND_CHIP_RESET and RTC**. | M | 22 |
| 25 | **TRNG and SHA-256**. | S | 11 |
| 26 | **OTP** (a stored array with ROM access; beyond that per Q10). | M | 6 |
| 27 | **POWMAN AON timer and LPOSC**. | M | 5 |
| 28 | **BUSCTRL performance counters, TBMAN, GLITCH_DETECTOR stores**. | S | 11 |
| 29 | **HSTX and the SIO TMDS encoder** (per Q9). | M | 11 |
| 30 | **Boot path** (IMAGE_DEF and partitions; per Q2). | L | 6, 25, 26 |
| 31 | **Non-secure TrustZone** (per Q3). | L | 2 |
| 32 | **USB host mode** (per Q4). | L | 20 |
| 33 | **CYW43439 over PIO gSPI** (DESIGN.md §7 "Later"; the `pico2w` LED). | L | 14 |

### Item 1, done: the Arm Pico 2 and its refusal census

`chip.ArmBoard` runs `examples/pico2/hello_serial.elf` and `blink.elf`, SDK 2.3.0 `no_flash` builds
for `pico2` / `rp2350-arm-s`, unmodified but for one documented entry setting
(`examples/pico2/README.md`). `chip/arm_tests.sysl` asserts the UART0 bytes, a line every 100 ms, and
GPIO 25 toggling every 250 ms of guest time.

**The entry.** An RP2350 RAM image entered by a debugger goes back through the boot ROM by default
(`PICO_CRT0_DEBUG_ENTRY_RESETS_VIA_BOOTROM`): it writes a RAM-image boot request into the watchdog's
scratch registers and vectors through the ROM's table at 0, which is the Raspberry Pi ROM's boot path
(items 6 and 30). The examples are built with it 0, the SDK's other supported entry, so crt0 sets up
the RCP and MSPLIM itself and vectors through its own table.

**Every access the chip or the core refused on the way to `main`, `hello_serial`'s output and
`blink`'s LED, in the order the programs make them, and what answers it now:**

| # | where | the access | refused as | handler now | item it belongs to |
|---|---|---|---|---|---|
| 1 | `crt0.S` `_entry_point` | CPACR ← CP7 full access | CP0–7 fields read as zero | CPACR keeps the fields of the coprocessors the board puts on the port (`v6m/t2system.sysl`, `Cpu.coprocessors`) | 3 |
| 2 | `crt0.S` | `mrc p7, #1, APSR_nzcv, c0, c0, #0`, the RCP salt status | undefined instruction | the coprocessor interface (`v6m/coproc.sysl`); the RCP answers this core's salt status as RCP true/false (`chip/rcp.sysl`) | 3 (done) |
| 3 | `crt0.S` | `mcrr p7, #8, r0, r0, c0` and `…, c1`, the salts | undefined instruction | the RCP stores each core's salt once; a second set is an RCP fault, which stops the core at `Fault`, named | 3 (done) |
| 4 | `runtime_init_usb_power_down` | USBCTRL SIE_CTRL read and SET-alias write, 0x5011004C | not on the bus | `UsbCtrl`, SIE_CTRL stored (`peripheral/rp2350/startup.sysl`) | 20 (minimum) |
| 5 | `runtime_init_boot_locks_reset` | BOOTRAM BOOTLOCK0–7 writes, 0x400E080C–0x400E0828 | not on the bus | `BootRam`, the whole block | 5 |
| 6 | `runtime_init_bootrom_locking_enable` | BOOTLOCK7 read (claim) | not on the bus | `BootRam` | 5 |
| 7 | `runtime_init_per_core_bootrom_reset` → `rom_func_lookup` | `tt r2, r2` (is the caller Non-secure?) | undefined instruction | `TT*` on a Secure-only core (`v6m/thumb2.sysl`, `Cpu.security`) | 2 (minimum) |
| 8 | `rom_func_lookup` | ROM halfword at 0x16, then a call into the ROM | not on the bus | benchtop's ROM, read-only at 0 (`chip/rom/rom.S`): the lookup, and `bootrom_state_reset` | 6 (minimum) |
| 9 | `runtime_init_per_core_enable_coprocessors` | CPACR ← CP0, CP4, CP10 | CP0 and CP4 read as zero | as 1 | 3, 7 |
| 10 | `runtime_init_per_core_enable_coprocessors` | `mrc p4, #0, r0, c0, c0, #1`, the DCP's RCMP | undefined instruction | the DCP answers this read 0; every other DCP instruction is refused by name | 7 (minimum) |
| 11 | `blink`: `gpio_init`, `gpio_set_dir`, `gpio_put` | `mcr p0, …` and `mcrr p0, #4, …` | undefined instruction | the GPIO coprocessor, in full: each instruction its SIO write or read | 3 |

Nothing else was refused: RESETS, CLOCKS, XOSC, the PLLs, TICKS, TIMER0, the SIO (spinlocks, GPIO),
IO_BANK0, PADS_BANK0, UART0 and the NVIC took every access the two programs make as they were.

### Item 5, done: the start-up register blocks

Census rows 5 and 6 (BOOTRAM's boot locks) were answered in item 1 and are tested complete here:
BOOTRAM is 1 KB, WRITE_ONCE0/1 and BOOTLOCK0-7 with BOOTLOCK_STAT. The new blocks, each in
`peripheral/rp2350/` and wired into `chip/rp2350.sysl` at the SVD base:

- **PSM** (`psm.sysl`). Stage n out of reset = not FRCE_OFF[n] and (FRCE_ON[n] or stage n-1 out of reset);
  DONE is that, bit 0 (PROC_COLD) first and bit 24 (PROC1) last. FRCE_OFF wins over FRCE_ON. Releases
  complete at once. The chip acts on PROC1 only: forced off, core 1 stops (`take_core1_reset`, both
  boards); released, the ROM wait restarts, core 1's mailbox is drained and a 0 goes to core 0's FIFO,
  which is what `multicore_reset_core1` waits for. Words core 0 sends while it is held sit unanswered.
  Forcing any other stage off is refused: `PSM FRCE_OFF of PROC0 is not modelled: only PROC1 can be forced
  off`. WDSEL is stored for the watchdog item.
- **ROSC** (`rosc.sysl`). Running at power-on. A wrong code in ENABLE, FREQ_RANGE, DORMANT, DIV or a
  password field is dropped (ENABLE: enables) and latches STATUS.BADWRITE (write 1 to clear). Frequency is
  the nominal 11 MHz while enabled and not DORMANT, else 0. **RANDOMBIT**: a 32-bit Galois LFSR (taps
  x^32+x^22+x^2+x+1, mask 0x80200003) seeded 0x3F04B16D at power-on and reloaded by any write to RANDOM;
  a read returns bit 0 then steps once (no step while the ring is stopped). The nth read after a load of S
  is bit 0 of the state n-1 steps on. COUNT counts the ring's cycles down and stops at 0.
- **SYSINFO**, **SYSCFG**: as in the matrix. PACKAGE_SEL is 1 (the Pico 2's QFN60 RP2350A).
- **POWMAN** (`powman.sysl`): a store behind the password (data bits 31:16 = 0x5AFE through any alias; a
  bad one is dropped and latches BADPASSWD). SCRATCH0-7 and BOOT0-3 take no password and all 32 bits. The
  regulator registers drop writes until VREG_CTRL.UNLOCK is set, and UNLOCK never clears. The AON timer,
  power states and power-up requests are refused by name for item 27.
- **ACCESSCTRL** (`accessctrl.sysl`): stored behind the 0xACCE password; the password rules for LOCK and
  CFGRESET and the lock being per core are assumptions from the register descriptions, since the SDK headers
  do not state them.
- **CLOCKS FC0**: a write of FC0_SRC measures at once. RESULT = source Hz x FC0_REF_KHZ x 32 / clk_ref Hz
  (kHz with 5 fractional bits); PASS/FAIL/SLOW/FAST compare the whole kHz with MIN and MAX. A stopped
  source or a stopped clk_ref finishes at once with DONE, DIED and FAIL (SLOW too where MIN_KHZ is above
  0) and a RESULT of 0, rather than WAITING for ever: that is an assumption, since the headers only say
  DIED is "test clock stopped during test". A source with no model (GPIN0/1, OTP's clock, the DFT tap, a
  value past 0x10) is refused at the FC0_SRC write by name, `CLOCKS FC0_SRC = 6 is not modelled: ...`, and
  FC0_SRC keeps its old value. clk_peri, clk_usb, clk_adc and
  clk_hstx are computed from AUXSRC, DIV and ENABLE/KILL.

`examples/pico2/clock_report.c` prints `clock_get_hz` against `frequency_count_khz` for the six
generators and the sources, the chip id, PSM DONE, 32 RANDOMBIT bits and the regulator; `chip/startup_tests.sysl`
asserts every line.

### Item 2, done: the M33's Secure-only minimum

`v6m/t2secure.sysl`, on a core with `Cpu.security` (the Arm board's), against the ARMv8-M ARM and
`regs/m33.h`:

- **Secure state only.** Every banked register is the Secure copy and every exception targets
  Secure state. `SG` is a NOP, as it is in Secure state.
- **The SAU** (`SAU_CTRL`, `SAU_TYPE` = 8, `SAU_RNR`, `SAU_RBAR`, `SAU_RLAR`) is stored; `SAU_RNR`
  past region 7 makes a region access a refusal. **SFSR** (write one to clear) and **SFAR** are
  stored, and so, now, are **CFSR** and **HFSR**: what benchtop records into them reads back, byte and
  halfword access to CFSR included.
- **`TT`, `TTT`, `TTA`, `TTAT`** answer from the SAU when they run: S, SRVALID and SREGION (an
  address in exactly one enabled region is Non-secure, or Secure where the region is NSC; the PPB is
  exempt), and R, RW, NSR and NSRW from the default memory map (no MPU is modelled; the PPB is not
  accessible unprivileged), only where privileged or `A`. No IDAU: IRVALID is never set.
- **Faults taken.** STKOF from any SP write below MSPLIM/PSPLIM, and INVPC from an EXC_RETURN with ES
  clear, are recorded in CFSR and taken: the UsageFault where SHCSR enables it and its priority
  preempts, HardFault with FORCED where not, and a lockup stop (`StackLimit`, `Fault`) where not even
  HardFault preempts. An exception entry whose frame crosses the limit stacks only above it, leaves
  SP at the limit and derives STKOF, taken at once where it outranks the exception (that one left
  pending) and pending otherwise. `CCR.STKOFHFNMIGN` ignores the limit at negative priority.
  SecureFault has its enable, its SHPR1 priority and its SHCSR active and pending bits; software
  pends it through SHCSR.
- **Refused by name** (`Stop.NonSecure`, `Cpu.transition`, "Non-secure state is not modelled"):
  `BXNS` and `BLXNS` to an address with bit 0 clear, an EXC_RETURN with S or DCRS clear, `MRS`/`MSR`
  of a `_NS` register, and any access to the PPB's Non-secure alias (0xE002_0000–0xE002_FFFF).
- **Still not modelled:** SecureFault INVTRAN (Secure code executing from memory the SAU marks
  Non-secure runs), the IDAU, and the MPU (item 31 and later).

`examples/pico2/stack_limit.c` sets MSPLIM 1 KB below SP and calls a function with a 2 KB frame: its
`sub.w sp` is the UsageFault STKOF in the program's own handler, which lifts the limit and returns,
and the `SUB` runs again. `examples/pico2/tt.c` prints ACLE's `cmse_TT`/`TTT`/`TTA`/`TTAT` of SRAM and
the PPB before and after two SAU regions. Both end on a `BKPT`; `chip/arm_tests.sysl` asserts their
UART0 text.

### Item 3, done: the redundancy coprocessor and the GPIO coprocessor

**The RCP (`chip/rcp.sysl`) runs every instruction `hardware/rcp.h` emits**, each in its delayed and
`…2` (nodelay) form, for each core against that core's own salt, sequence counter and PRNG:
`rcp_salt_core0`/`1` (either core writes either, once), `rcp_canary_status`, `rcp_canary_get` and
`rcp_canary_check`, `rcp_bvalid`, `rcp_btrue`, `rcp_bfalse`, `rcp_b2valid`, `rcp_b2and`, `rcp_b2or`,
`rcp_bxorvalid`, `rcp_bxortrue`, `rcp_bxorfalse`, `rcp_ivalid`, `rcp_iequal`, `rcp_random_byte`,
`rcp_count_set`, `rcp_count_check` and `rcp_panic`.

- **Faults.** A failed check, `rcp_panic`, a second salt, and any instruction but the salts and the
  status before the issuing core's salt is set are RCP faults. On the chip the core stalls on the
  coprocessor port for good (NMI if an interrupt abandons the stalled instruction). benchtop stops the
  core at `Fault` **on the instruction**, and the run's reason names the check and its operands, e.g.
  `core 0 at pc 0x20000310: the RCP faults: core 0's rcp_canary_check(90) was given 0x…, not the
  canary for tag 90`. The stall-then-NMI itself is not modelled: the core never runs again either way.
- **Random delays take no time**, so runs stay deterministic; each delayed instruction still steps
  the issuing core's PRNG once, as the delay draws from it, so `rcp_random_byte` reads the sequence
  the same instructions give on a chip.
- **What the datasheet does not publish** is benchtop's own: the canary function (a SplitMix64 mix
  of salt and tag) and the PRNG's recurrence (a full-period 24-bit congruential one seeded from the
  salt's top 24 bits). Only the values differ from a chip's; every equality and inequality a program
  can observe holds.
- **Refused by name**: an encoding outside the RCP's set (a check with another's CRm or opc2, an
  `MRRC`, a third salt), since the datasheet does not say what the RCP does with one.

**The GPIO coprocessor** was checked against every one of `gpio_coproc.h`'s 63 functions by
`examples/pico2/gpioc.c`, which compares each with the SIO write or read the header documents it as
(87 checks, 0 mismatches). GPIO 48–63 reach the SIO's high word, whose bits 16–31 (QSPI and USB pads)
the SIO model does not hold; that is the SIO's, not the coprocessor's.

**A coprocessor stop is on the instruction** under both executors (`v6m/coproc.sysl`), refusal or
fault. Tests: `chip/rcp_tests.sysl` (each instruction, its fault message, the edges), the executor
tests and the two SDK programs in `chip/arm_tests.sysl`: `rcp.elf` runs every `rcp_*` function in
both forms clean, then corrupts a canary and stops at `Fault` with the check named.

### Item 15, done: the doorbells and the interpolators

**The interpolator** (`peripheral/rp2040/interp.sysl`) is one model for both chips, against the
RP2040 and RP2350 `hardware/regs/sio.h` INTERP0_*/INTERP1_* descriptions and `hardware_interp`. Each
RP2350 core's SIO holds its own INTERP0 and INTERP1 (`Sio.interps`); the RP2040's will hold the same
when its chip layer places them. The one difference between the chips is the lane's SHIFT: a logical
right shift on the RP2040, a right rotate on the RP2350 (each chip's `sio.h` says so).

- Each lane: its own accumulator or, with CROSS_INPUT, the other's; shifted or rotated; bits
  MASK_LSB..MASK_MSB kept (MASK_LSB above MASK_MSB keeps none, signed or not); sign-extended from
  MASK_MSB with SIGNED. The result is BASE plus that, or plus the raw input with ADD_RAW (CROSS_INPUT
  is ahead of the bypass). FULL is BASE2 plus both lanes' values, whatever ADD_RAW says.
- PEEK reads; POP reads and writes both lane results back, each into the other accumulator with
  CROSS_RESULT. FORCE_MSB is ORed into bits 29:28 of a lane as the bus reads it only.
- **BLEND** (INTERP0's lane 0 only): lane 1 is `(BASE0·(256−α) + BASE1·α) >> 8`, signed by lane 1's
  SIGNED (rounding down), α the 8 low bits of lane 1's value; lane 0 is α; FULL leaves lane 1 out.
  **CLAMP** (INTERP1's lane 0 only): lane 0's value held to BASE0 below, then BASE1 above (so BASE0
  wins where the bounds cross), compared signed by lane 0's SIGNED, nothing added. Each interpolator
  stores only its own mode bit.
- OVERF0/OVERF1/OVERF (read-only, lane 0's CTRL): a lane's shifted input has a bit above MASK_MSB.
  ACCUMx_ADD adds its 24-bit field (bits 23:0, as `sio.h` gives it) to the whole accumulator and reads
  the lane's shift-and-mask value. BASE_1AND0 sign-extends each half by its lane's SIGNED and reads 0
  (`interp_hw_t.base01` is write-only). A write to PEEK or POP does nothing.

**The doorbells** (RP2350 only): DOORBELL_OUT_SET/CLR write the other core's DOORBELL_IN and read it;
DOORBELL_IN_SET/CLR this core's; bits 7:0. SIO_IRQ_BELL (26) is raised in a core while its
DOORBELL_IN is non-zero (`chip/rp2350.sysl` `signal`). SIO_IRQ_BELL_NS and PERI_NONSEC are Non-secure
(Q3) and refused by name with the TMDS encoder (item 29).

Tests: `peripheral/rp2040/interp_tests.sysl` (every mode on both chips' shift: reset and the stored
CTRL bits, shift 0 and 31, mask edges, signed, ADD_RAW, cross both ways, POP and FULL, FORCE_MSB,
blend unsigned and signed, clamp at and past its bounds, BASE_1AND0, ACCUMx_ADD, OVERF);
`peripheral/rp2350/sio_tests.sysl` (each core's own pair, both counting at once, access widths, every
doorbell register); `chip/interp_tests.sysl`: core 0's loads and stores under both executors, the bell
pending in core 1's NVIC only, and `examples/pico2/interp.c`, which prints each mode's answers through
`hardware_interp` (the test holds the values worked by hand from the datasheet), launches core 1 to
count on its own INTERP0 while core 0 counts on its, then rings doorbells: core 0 on itself, core 0 on
core 1, whose SIO_IRQ_BELL handler acknowledges and rings back.

### Item 4, done: XIP flash, the cache and the QMI

`examples/pico2` adds flash builds of `hello_serial` and `blink` (and `hello_serial_flash.uf2`),
`qmi_flash`, which reprograms a sector of its own flash through direct mode without the ROM and reads
it back through every window, and `xip_sram`, the SDK's `xip_ram` binary type run whole from the XIP
cache's 16 KB as `pico_xip_sram_test` is. `chip/xip_tests.sysl` runs all of them and every edge of the
board's map.

**The memory map.** The cores keep their fast path: SRAM; the flash, read and executed in place from
the first 4 MB of the cached window; the boot ROM, now a read-only RAM region (`v6m` `read_only`), so a
store to it is refused by name; and each XIP_SRAM page whose every cache line is pinned. The board
mirrors the chip (`ArmBoard.sync_xip`): a program or erase makes code decoded from the bytes decode
again (`v6m` `flash_changed`), direct mode takes the flash off the fast path, so a memory-mapped access
is the bus error silicon gives, and a page with a line unpinned leaves the fast path, so each access is
checked by line and an unpinned one refused. Every other XIP address goes through the chip.

**The loader** (`ArmBoard.boot`) writes an ELF's PT_LOAD segments at their physical addresses, so a
flash image's `.data` load image is in flash for crt0 to copy, and a UF2's blocks at their targets,
refusing a wrong magic, a missing or foreign family and an address off the memory by name, and
dropping picotool's RP2350-E10 block. An image that lands in XIP_SRAM has its lines pinned first, as
the ROM pins them for an image whose block asks. A flash UF2 starts as the ROM hands one over (salts set,
VTOR, SP and PC from the vector table at XIP_BASE); one placed elsewhere in flash needs the ROM's
partition search (item 30).

**Not run, by name:** CTR_HIT and CTR_ACC (no lookups are simulated); a pin over flash; a write
through XIP with WRITABLE_M0 set (a QMI write command); QSPI chip select 1; ATRANS0-3 other than the
identity; the flash's instructions other than those `parts/qspi_flash` lists, and a program or erase
under block protection. **Fidelity limits, not refused:** the cache never makes a read stale, so a
missing maintenance operation goes unseen; a store into the cached window's first 4 MB stops the core
at `Fault` (the core's read-only flash) where silicon drops it; code already decoded keeps running
from flash in direct mode, and from an XIP_SRAM line unpinned after it was decoded; QMI transfers take
no time. The SDK's `flash_range_erase`/`flash_range_program`/`flash_get_unique_id` reach the ROM's
`connect_internal_flash`, `flash_exit_xip`, `flash_range_*` and `flash_flush_cache`, which are item 6;
until then the ROM's lookup answers 0 for them.
