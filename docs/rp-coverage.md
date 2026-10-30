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
  (`boards/pico2.hocon`), with the M33s' coprocessors on their ports (`chip/coprocessors.sysl`). Both
  boards map Raspberry Pi's own A4 boot ROM at address 0 (`chip/rom/`, `chip/bootrom.sysl`; item 6
  below). On the RISC-V board the image is an
  ELF loaded into SRAM. The Arm board also has the Pico 2's 4 MB flash behind XIP_BASE and the XIP
  cache's lines as SRAM, and loads an ELF or a UF2 into SRAM, flash or XIP_SRAM (item 4 below).
  Every other address is refused by name. The SDK's
  `hello_serial` and `blink` run unmodified on the Arm board as `no_flash` and as flash builds
  (`examples/pico2`, items 1 and 4).
- **The RP2040 runs on its two Cortex-M0+ cores** (item 22): `chip.Rp2040` (`chip/rp2040.sysl`) on
  `ArmBoard[Rp2040]`, the `pico` board, over the RP2350's shared models, with a stand-in boot ROM that
  carries only core 1's launch wait. The SDK's `hello_serial`, `blink` and pico-examples'
  `hello_multicore` run as `no_flash` images (`examples/pico`). Its RESETS, TIMER and watchdog tick
  generator are also library peripherals (`rp-resets`, `rp-timer`, `rp-watchdog`) that run on the
  `virt*` boards through `virt/rp.sysl`, with clk_ref taken to be the core clock because the virtual
  boards have no CLOCKS block (`virt/rp.sysl:8–12`). `virt-sio` (`virt/sio.sysl`) is
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
| M33 MPU (PMSAv8, 8 regions per state) | 3.7 | partial (stored) | On a Secure-only core: MPU_TYPE (8), CTRL, RNR (three bits), RBAR, RLAR and their three aliases, MAIR0/1 stored; `TT` answers MREGION, MRVALID, R and RW from them, an overlap naming no region (`v6m/t2secure.sysl`, item 6). **Not enforced**: an access the regions forbid is made where silicon takes MemManage. The Non-secure MPU is not built (Q3). | `v6m/t2rom_tests.sysl` | M (enforcement) | — |
| M33 stack limits (MSPLIM/PSPLIM) | 3.7 | full | Every SP write below the limit is the UsageFault STKOF, recorded in CFSR and taken where SHCSR enables it and it preempts, else HardFault (HFSR.FORCED), else a lockup stop; exception stacking that crosses the limit stacks only above it, leaves SP at it and derives the fault (late arrival or pending); `CCR.STKOFHFNMIGN` at negative priority (`v6m/t2secure.sysl`, item 2). | `t2_except.sysl` stack-limit tests; `examples/pico2/stack_limit.c` in `chip/arm_tests.sysl` | — | — |
| TrustZone, Secure-only minimum (SAU stored, TT*, SecureFault, NS transitions refused) | 3.7, 10 Security | full | Secure reset state; SAU_CTRL/TYPE/RNR/RBAR/RLAR stored (8 regions); `TT`/`TTT`/`TTA`/`TTAT` answer from the SAU, the IDAU's one numbered region (the ROM's secure gateways, 0x7e00–0x7fff, region 2, NSC) and the MPU (item 6); NSACR, SCR and the NS alias's CPACR stored; SecureFault's enable, priority, SFSR (W1C) and SFAR, pended through SHCSR; `SG` a NOP; `BXNS`/`BLXNS` to Non-secure, EXC_RETURN with S or DCRS clear, `MRS`/`MSR` of `_NS` registers and the PPB's NS alias stop at `NonSecure`, named; EXC_RETURN with ES clear is UsageFault INVPC (item 2). Not detected: INVTRAN (executing from SAU-Non-secure memory). | `v6m/t2secure_tests.sysl`; `examples/pico2/tt.c` in `chip/arm_tests.sysl` | — | — |
| TrustZone, Non-secure state | 3.7, 10 Security | full | Both states' banked registers, the SCS's Non-secure alias with its own SysTick, SAU and IDAU enforced on fetches and data, SG/BXNS/BLXNS/FNC_RETURN, cross-state exceptions with the integrity signature, NVIC_ITNS, SecureFault INVEP/INVIS/INVER/AUVIOL/INVTRAN. Refused by name: FP context across states, BFHFNMINS/PRIS, a Non-secure PRIGROUP of its own, the SIO's Non-secure bank. See "Item 31, done". | `v6m/t2nonsecure_tests.sysl`, `chip/arm_tests.sysl` (`nonsecure.elf`) | — | — |
| M33 coprocessors: GPIOC (CP0), DCP (CP4/5), RCP (CP7) | 3.6 Cortex-M33 coprocessors | full | `MCR`, `MRC`, `MCRR`, `MRRC`, `CDP` and their `…2` forms reach the board through `Bus.coprocessor`, gated by CPACR, which keeps the fields of the ports the board fills (`v6m/coproc.sysl`). **GPIOC is full**: every instruction of `gpio_coproc.h` is its SIO equivalent (`chip/coprocessors.sysl`). **RCP is full** (item 3): salts, canaries, boolean and integer checks, the sequence counter, the random byte and `rcp_panic`, each core its own state; a failed check stops the core at `Fault` with the check named (`chip/rcp.sysl`). **DCP is full** (item 7): every instruction of `dcp_instr.inc.S` in its `R…` and `P…` forms on CP4 and CP5, each core's two instances its own, so every canned sequence gives its IEEE result (`chip/dcp.sysl`). Any encoding outside a coprocessor's set is refused by name. | `v6m/coproc_tests.sysl`, `chip/rcp_tests.sysl`, `chip/dcp_tests.sysl` (`dcp.elf`), `chip/arm_tests.sysl` (`rcp.elf`, `gpioc.elf`) | — | — |
| Hazard3 ×2 (RV32IMAC + Zb*/Zc*/Zcmp, PMP, Xh3pmpm, Xh3power) | 3.8 Hazard3 | full | `riscv/rp2350.sysl:6–22` and `riscv/HAZARD3-COVERAGE.md`. | `riscv/hazard3_*_tests`, `chip/tests.sysl:18` | — | — |
| Hazard3 interrupt controller (Xh3irq) | 3.8 | full | MEIEA, MEIPA, MEINEXT and MEICONTEXT (`riscv/csr.sysl:34–42`). Only the lines `signal` drives ever pend (see "Interrupt routing"). | `riscv/hazard3_tests` | — | — |
| Architecture switching (ARCHSEL, both pairs) | 3.9 Architecture switching | missing | The board fixes the cores (`boards/pico2-riscv.hocon` against `pico2`). OTP's ARCHSEL and ARCHSEL_STATUS report the board's architecture; an ARCHSEL write naming the other is refused by name (item 26). | `chip/otp_tests.sysl` | S | Arm board, boot path |
| Interrupt routing (52 lines) | 3.2 Interrupts | partial | Wired: TIMER0/1 ×4, SIO_FIFO, SIO_BELL, POWMAN_IRQ_POW and POWMAN_IRQ_TIMER, UART0 and UART1, plus MTIME and the soft IRQ (`chip/rp2350.sysl:325–336`). Every other source is absent with its block. | `chip/tests.sysl:18` | grows with each block | — |
| SIO: CPUID, GPIO (both banks), FIFOs (4-deep), spinlocks, FIFO IRQ | 3.1 SIO | full | `peripheral/rp2350/sio.sysl:6–11,13,39–60`. The E2 erratum option (DESIGN.md §9.3) is not built. | `peripheral/rp2350/tests.sysl:129`, `chip/tests.sysl:94,115` | — | — |
| SIO RISC-V platform timer (MTIME, MTIMECMP, soft IRQ) | 3.1.8 / 3.1 SIO | full | `peripheral/rp2350/sio.sysl:15–16,48–51`. | `rp2350/tests.sysl:129` | — | — |
| SIO doorbells (8, SIO_IRQ_BELL) | 3.1 SIO | full | DOORBELL_OUT_SET/CLR (the other core's DOORBELL_IN) and DOORBELL_IN_SET/CLR (this core's), bits 7:0; SIO_IRQ_BELL (26) per core while any bit is set (`peripheral/rp2350/sio.sysl`, `chip/rp2350.sysl` `signal`; item 15). The Non-secure SIO's SIO_IRQ_BELL_NS is not (Secure-only). | `rp2350/sio_tests.sysl`, `chip/interp_tests.sysl` (`interp.elf`) | — | — |
| SIO interpolators INTERP0/1 (per core) | 3.1 SIO | full | `peripheral/rp2040/interp.sysl`, shared with the RP2040: each core's own pair; shift (rotate here), mask, SIGNED, CROSS_INPUT, CROSS_RESULT, ADD_RAW, FORCE_MSB, BLEND (INTERP0), CLAMP (INTERP1), OVERF flags, PEEK/POP, ACCUMx_ADD, BASE_1AND0 (item 15). | `rp2040/interp_tests.sysl`, `rp2350/sio_tests.sysl`, `chip/interp_tests.sysl` (`interp.elf`) | — | — |
| SIO TMDS encoder | 3.1 SIO | missing | Refused (`sio.sysl:10`). | — | S | HSTX (for a use) |
| BUSCTRL: bus priority, performance counters | 2.1 Bus fabric | missing | Not decoded. | — | S (counters) | DMA/cores for events |
| ACCESSCTRL | 10.6 Access control | full (stored) | Every register stored with the datasheet's reset values; the 0xACCE password (not GPIO_NSMASK0/1), sticky LOCK bits that drop a locked core's writes, FORCE_CORE_NS, CFGRESET. The Arm board gates every core bus access by it (master, security state, privilege), a denial refused by name; a write restricting a memory device is refused. | `rp2350/sysctl_tests.sysl`, `chip/startup_tests.sysl`, `chip/arm_tests.sysl` | — | — |
| ROM: core-1 launch protocol | 5.3 / 5 Bootrom | full | The real ROM's own code, on both boards: each M33's and Hazard3's core 1 starts from reset in it and waits there (salts, then the FIFO handshake, asleep in `WFE` / `h3.block`); PSM's release restarts it (item 6). | `chip/arm_tests.sysl`, `chip/startup_tests.sysl` | — | — |
| ROM: contents and API (`rom_func_lookup`, flash ops, `reset_usb_boot`, `get_sys_info`, OTP access) | 5.4 Bootrom APIs | partial | Raspberry Pi's A4 image, whole, at 0 on both boards (`chip/rom/`); every function runs as the ROM's code. Run end to end: the lookup, `get_sys_info`'s BOOT_INFO, CHIP_INFO, CRITICAL and CPU_INFO, `connect_internal_flash`, `flash_exit_xip`, `flash_range_erase`/`_program`, `flash_flush_cache`, the unique ID, `otp_access` (reads and writes, raw and ECC, its lock and argument checks; item 26), `bootrom_state_reset` and `reboot` (PC/SP). Each other function runs until it reaches a block not modelled, which refuses it by name: USB (`reset_usb_boot`), the partition and image calls (item 30). | `chip/arm_tests.sysl` (`rom.elf`), `chip/otp_tests.sysl` (`otp.elf`) | M | USB (20), item 30 |
| Boot path: IMAGE_DEF, partition tables, UF2, secure-boot flags, signature check | 5 Bootrom, 10 Security | missing | Core 0 does not run the ROM's boot path: power-on and the watchdog's reset hand an image over as it would (`chip/arm_board.sysl`), writing BOOT_INFO and the chip ID it copies from the OTP where the ROM keeps them. It needs TRNG and SHA-256 (the boot salts) and the image parsing; the OTP's boot flags and critical flags it reads are modelled (item 26). | — | L | ROM, XIP, SHA-256 |
| SRAM 0–9 (520 KB) | 4.2 SRAM | full | `mcus/rp2350.hocon:19`. Striping does not matter to a program. | every chip test | — | — |
| BOOTRAM (with boot locks) | 4.3 Boot RAM | full | 1 KB of SRAM, WRITE_ONCE0/1 and the eight boot locks with BOOTLOCK_STAT (`peripheral/rp2350/startup.sysl`). | `peripheral/rp2350/startup_tests.sysl` | — | — |
| XIP: flash window, aliases, cache, cache-as-SRAM, XIP_CTRL, streaming FIFO | 4.4 XIP | full | One 4 MB flash array (`parts/qspi_flash`, a W25Q32RV) behind the cached, uncached and untranslated windows, read and executed in place on the cores' fast path from the cached window's first 4 MB (`chip/arm_board.sysl`); the rest through the chip, translated by ATRANS and wrapping at the device's size (`peripheral/rp2350/xip.sysl`). XIP_CTRL in full, the cache's tags with every maintenance operation, lines pinned over XIP_SRAM holding their bytes (a fully pinned page on the fast path, a partly pinned one checked line by line), the streaming FIFO and XIP_AUX. Fidelity limits: the cache is functional, not timed (no lookups, so CTR_HIT/CTR_ACC are refused by name and a read is never stale); a pin over flash, a write through XIP with WRITABLE_M0 set, and code fetched from flash in direct mode or from an unpinned XIP_SRAM line are the cases not run or not refused (item 4 below). | `peripheral/rp2350/xip_tests.sysl`, `chip/xip_tests.sysl` | — | — |
| QMI (flash and PSRAM interface; direct mode) | 12.14 QMI | full | Direct mode with both FIFOs, NOPUSH, 16-bit entries, ASSERT and AUTO chip selects, RX-full stall and BUSY, all to the flash on CS0; the window, timing and ATRANS registers stored. Instantaneous: no clock divider timing. CS1 (no PSRAM on the Pico 2) and a non-identity ATRANS0-3 are refused by name. Clocks with no chip select reach no device, and a frame whose instruction comes at dual or quad width is ignored by the SPI-mode flash, as the ROM's `flash_exit_xip` relies on (item 6); the ROM's flash routines run on it. | `peripheral/rp2350/xip_tests.sysl`, `chip/xip_tests.sysl` (`qmi_flash`) | — | — |
| OTP | 13 OTP | full | The array of 4096 rows, the board's for the run (a factory image of CHIPID and RANDID; a boot starts it again, a reset keeps it); the four read windows with the ECC (SECDED, one wrong bit corrected, BRBP inversion) and the guarded windows' bus faults refused by name; SW_LOCK0–63, loaded from the PAGEn_LOCK1 rows by majority and only advanced; the SBPI bridge as the ROM's `otp_access` drives it, programming a row's bits from 0 to 1; USR; CRITICAL and KEY_VALID (0); ARCHSEL and ARCHSEL_STATUS (`peripheral/rp2350/otp.sysl`, item 26). Refused by name: programming the critical flag rows, the hardware keys and key locks, any other SBPI instruction, an uncorrectable row through the unguarded window, ARCHSEL naming another architecture, DBG, BIST, CRT_KEY, DEBUGEN(_LOCK), BOOTDIS and the interrupt registers. | `peripheral/rp2350/otp_tests.sysl`, `chip/otp_tests.sysl` (`otp.elf`), `chip/tests.sysl` | S (keys, debug enables, its interrupts) | — |
| POWMAN: the AON timer, LPOSC, VREG and BOD control | 6 Power | full | The store: VREG (locked until UNLOCK, VSEL limit), BOD, LPOSC (TRIM, MODE) and XOSC settings, SEQ_CFG, CHIP_RESET (HAD_POR), WDSEL, SCRATCH/BOOT, the interrupt registers into IRQs 44 and 45, all behind the 0x5AFE password; a 1 clears a W1C bit through any alias. The AON timer (item 27): 64-bit milliseconds loaded from SET_TIME by RUN, CLEAR, READ_TIME live (the SDK's re-read catches the carry); from the LPOSC (nominal 32.768 kHz) or the crystal at the rate LPOSC_/XOSC_FREQ_KHZ state, from GPIO 12/20/14/22 one millisecond per rising edge, or synchronised to a 1 Hz edge; the 64-bit alarm latching TIMER.ALARM into INTR.TIMER and IRQ 45, and a sleeping core skipping to it (`peripheral/rp2350/powman.sysl`). | `rp2350/aon_tests.sysl`, `rp2350/sysctl_tests.sysl`, `chip/aon_tests.sysl` (`aon_timer.elf`), `chip/startup_tests.sysl` | — | — |
| POWMAN power states and power-up requests | 6.2 Power management | full | STATE's requests judged as the header describes (WAITING for the processors, BAD_SW_REQ, REQ_IGNORED, PWRUP_WHILE_WAITING into POWMAN_IRQ_POW), PWRUP0–3 level and edge requests with RAW_STATUS and STATUS, the alarm request, CURRENT_PWRUP_REQ, LAST_SWCORE_PWRUP, BOOTDIS; P1.x entered once every core halts, the switched core back at power-on and the SRAM banks and XIP cache it switches off cleared, time skipped to the alarm (Q7), GPIO power-ups through the pads' isolation latches and from outside the chip, and core 0 booted again as the ROM's power-up path does without the ROM (item 34). Refused by name: an SRAM bank switched off under the running switched core (P0 with a bank off), a PWRUP on a QSPI pin, a BOOT0–3 power-up vector (item 6), and the RISC-V harts' power-down (Q13). | `rp2350/power_tests.sysl`, `chip/power_tests.sysl` (`powman.elf`) | — | — |
| GLITCH_DETECTOR | 10.9 Glitch detector | missing | Not decoded. | — | S (store) | — |
| RESETS (29 outputs) | 7.5 Subsystem resets | full | `peripheral/rp2350/timing.sysl:15–22` over `rp2040/resets.sysl:7–11`. Release completes at once. | `rp2350/tests.sysl:15`, `chip/tests.sysl:43,81` | — | — |
| PSM (power-on state machine; FRCE_ON/FRCE_OFF/DONE) | 7.4 Power-on state machine | full | The 25-stage sequence: FRCE_OFF holds a stage and those after it, FRCE_ON releases from a later stage, DONE is computed, WDSEL stored. PROC1 forced off stops core 1 and its release re-enters the ROM wait with a 0 for core 0 (`multicore_reset_core1`). Forcing any other stage off is refused by name. | `rp2350/sysctl_tests.sysl`, `chip/startup_tests.sysl` | — | — |
| CLOCKS | 8.1 Clocks | partial | CTRL and DIV are stored. clk_ref and clk_sys follow their muxes and drive time. SELECTED is one-hot (`peripheral/rp2350/clocks.sysl:25–30`). The frequency counter FC0 is run (it measures the modelled tree, finishing at once); resus is stored. clk_peri, clk_usb, clk_adc and clk_hstx have frequencies from their AUXSRC and DIV, which FC0 reads, but no consumer. GPOUT is not driven on a pin. | `rp2350/tests.sysl:112`, `rp2350/sysctl_tests.sysl`, `chip/startup_tests.sysl` | S | consumers |
| XOSC | 8.2 Crystal oscillator | partial | STABLE after STARTUP.DELAY×256 (`clocks.sysl:97–105`). DORMANT and COUNT are not modelled; writing DORMANT's dormant value is refused by name (item 9). | `rp2350/tests.sysl:81` | S | — |
| ROSC (including RANDOMBIT) | 8.3 Ring oscillator | full | Every register, the code-protected fields with BADWRITE, DORMANT, COUNT. Frequency stays the nominal 11 MHz (Q8) and follows ENABLE and DORMANT. RANDOMBIT is a seeded LFSR (seed 0x3F04B16D at power-on, reloaded by RANDOM, one step per read). | `rp2350/sysctl_tests.sysl`, `chip/startup_tests.sysl` | — | — |
| TICKS (6 generators) | 8.4 Tick generators | full | `peripheral/rp2350/timing.sysl:24–90`. | `rp2350/tests.sysl:25` | — | — |
| PLL_SYS, PLL_USB | 8.6 PLL | full | LOCK once powered with an FBDIV. The output frequency is computed (`clocks.sysl:167–225`). Lock is instant. | `rp2350/tests.sysl:81` | — | — |
| IO_BANK0: FUNCSEL, overrides, STATUS | 9 GPIO | full | 48 GPIOs, one routing layer (item 9): FUNCSEL picks a producer (`route`), each offers a level and an output enable, OUTOVER/OEOVER then the pad decide; INOVER feeds the functions (SIO's GPIO_IN), IRQOVER the interrupts; STATUS with IRQTOPROC (`peripheral/rp2350/gpio.sysl`, `chip/rp2350.sysl` `drive`). Producers: SIO, NULL, and each UART's TX (idling high; bytes leave at once, item 8) and RX on F2 and F11. Every other function, and a FUNCSEL that is no function of its GPIO, is refused by name when selected, naming the item that builds it (`unmodelled_function`). | `chip/gpio_tests.sysl`, `rp2350/tests.sysl:183` | — | each peripheral adds its producer |
| GPIO interrupts (PROC0/1 INTE/INTF/INTS, edge latch, IO_IRQ_BANK0, dormant wake) | 9 GPIO | full | INTR's levels and latched edges (write 1 to clear through any alias), INTE/INTF/INTS for processor 0, processor 1 and the dormant wake, IRQSUMMARY (Secure only, Q3); processor k's line is IO_IRQ_BANK0 (21) in core k on both boards, the dormant wake's wakes a DORMANT ROSC. Edges latch whenever a GPIO's input can move: a function, override, pad or SIO write, the GPIO coprocessor, or a level driven from outside; none while IO_BANK0 is held (`peripheral/rp2350/gpio_irq.sysl`, shared with the RP2040's layout; item 9). IO_IRQ_BANK0_NS is never raised (Q3). | `rp2350/gpio_irq_tests.sysl`, `chip/gpio_tests.sysl` (`hello_gpio_irq.elf`, `gpio_irq.elf`, `pico2-riscv/gpio_irq.elf`) | — | — |
| PADS_BANK0 (ISO, IE, OD, pulls, drive, slew) | 9.8 Pads | full | `gpio.sysl:82–131`. Drive and slew are stored. | `rp2350/tests.sysl:169` | — | — |
| IO_QSPI, PADS_QSPI (including the BOOTSEL read via QSPI_SS) | 9 GPIO | partial (stored) | PADS_QSPI's seven registers and IO_QSPI's eight CTRLs and interrupt registers stored with their reset values and aliases, behind their RESETS bits, which the boards' boot releases (`peripheral/rp2350/qspi_pins.sysl`, item 6). The pins drive nothing (the flash is the QMI's); each STATUS, and so the BOOTSEL read, refused by name. | `peripheral/rp2350/rom_blocks_tests.sysl`, `chip/tests.sysl` | S (levels, BOOTSEL) | XIP for the shared pins |
| UART0/1 (PL011) | 12.1 UART | full | Item 8: frames timed from clk_peri at the divisor LCR_H latched, through `next_event`; the bits on the TX pin through the mux, frames over wires between GPIOs and from off the chip; FIFOs or holding registers, the IFLS trigger levels, the RT timeout, break both ways, FE/PE/BE/OE, LBE loopback, the TX and RX DREQs (`peripheral/rp2350/uart.sysl`, `chip/rp2350.sysl` `advance_lines`). Refused by name: IrDA, hardware flow control (CTSEN/RTSEN, and the CTS and RTS functions in the mux), reserved IFLS levels, a character with IBRD 0 latched, and a frame its pins cannot show. Modem status reads 0. | `rp2350/uart_tests.sysl`, `chip/uart_tests.sysl` | — | DMA consumes the DREQs (item 11) |
| SPI0/1 (PL022) | 12.3 SPI | missing | Not on the chip's bus. The PL022 model exists (`peripheral/spi/spi.sysl`) and runs on STM32 and virtual boards. | `peripheral/spi/tests.sysl` (6) | S (place) + S (DREQ) | mux, DMA |
| I2C0/1 (DW_apb_i2c) | 12.2 I2C | missing | `dw-i2c` has `modelled = false` (`registry.sysl:322`). The device callbacks exist (`peripheral/i2c/i2c.sysl`). | — | M (master), S (slave) | mux, DMA |
| PWM (12 slices) | 12.5 PWM | missing | Only `virt-pwm` (`peripheral/pwm/pwm.sysl`). | — | M | mux, DMA, IRQ |
| ADC and temperature sensor | 12.4 ADC | missing | `rp-adc` has `modelled = false` (`registry.sysl:324`). The analogue input path exists in `peripheral/adc/adc.sysl`. | — | M | clk_adc, DMA |
| DMA: 16 channels, DREQs, pacing timers, chaining, ring, IRQ0–3, sniff/CRC, MPU/secure attributes | 12.6 DMA | missing | Not decoded. | — | L (core) + S (sniff/CRC) + S (security attrs) | DREQs from every peripheral |
| PIO0/1/2 | 11 PIO | partial | The state machines are full (item 13): every PIO version 1 instruction, delay and side-set, the FIFOs and their joins (FJOIN_RX_GET/PUT too), the shift registers' thresholds, the fractional divider cycle-exact against clk_sys, EXECCTRL, SM_RESTART/CLKDIV_RESTART and their PREV/NEXT forms, SMx_INSTR, the IRQ flags across blocks, INTR and both interrupt lines' INTE/INTF/INTS, FDEBUG, FLEVEL, DBG_PADOUT/PADOE and GPIOBASE (`peripheral/rp2350/pio.sysl`). The pins are a vector of their own: FUNCSEL PIO0–2 stays refused, and the DREQs and the lines into the NVIC are item 14's. | `rp2350/pio_tests.sysl`, `chip/pio_tests.sysl` (`pio_programs.elf`: `hello_pio`, `ws2812`, `uart_tx`, `uart_rx`, the RP2350-only pair) | M (GPIO/DMA/IRQ) | mux, DMA, IRQ |
| USB controller, device mode (and the CDC bridge to the host) | 12.7 USB | full | The controller in device mode, whole (`peripheral/rp2350/usb.sysl`, item 20): the registers, DPRAM with the SETUP packet, endpoint and buffer control, single and double buffering, NAK, STALL, abort, the interrupts and USBCTRL_IRQ, driven by a host through the Arm board's `usb_*` seam. Not gated by RESETS, since the SDK reads SIE_CTRL before releasing USBCTRL; RESETS asserting it resets the controller. benchtop's own host (`chip/usb_host.sysl`, item 21) enumerates a CDC ACM device on a 1 ms frame schedule and bridges its port to the console or a pty (`benchtop run --usb`). | `peripheral/rp2350/usb_tests.sysl`, `chip/usb_tests.sysl`, `chip/usb_host_tests.sysl`, `host/pty_tests.sysl` | — | clk_usb |
| USB controller, host mode | 12.7 USB | missing | — | — | L | device mode |
| TIMER0/1 | 12.8 System timers | full | 64-bit counter, latch, 4 alarms, LOCKED, SOURCE (`peripheral/rp2350/timing.sysl:91–145`). | `rp2350/tests.sysl:44`, `chip/tests.sysl:18` | — | — |
| WATCHDOG | 12.9 Watchdog | full | The RP2040's counter (`peripheral/rp2040/watchdog.sysl`, `WatchdogCounter`), one decrement a tick from TICKS' watchdog generator: CTRL (ENABLE, PAUSE_DBG0/DBG1/JTAG, TIME), LOAD, TRIGGER, REASON, SCRATCH0–7. Firing resets the PSM stages WDSEL selects, and RESETS.WDSEL's peripherals where RESETS' stage is not selected (`Rp2350.watchdog_reset`); the Arm board restarts core 0 through the ROM's boot path (the PC/SP request in SCRATCH4–7, or the flash image's normal boot) and core 1 into the ROM wait; a typed boot request and a blank flash's BOOTSEL are refused by name; the RISC-V board refuses the restart by name. No debugger exists to pause it, so the PAUSE inputs are only tested at the model (item 10). | `rp2040/watchdog_tests.sysl`, `rp2350/tests.sysl`, `chip/watchdog_tests.sysl` (`watchdog.elf`) | — | — |
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
| Chip layer: bus decode, clock tree, IRQ routing, Pico board | 2.2 Address map | full | `chip.Rp2040` (`chip/rp2040.sysl`) on `ArmBoard[Rp2040]`, the `pico` board (item 22). Every block not on the chip yet is refused by name with its item. | `chip/pico_tests.sysl` | — | — |
| Cortex-M0+ ×2 | 2.4 Cortex-M0+ | partial | v6m on the Pico, on the instruction timing table (the SDK's divider wait needs it), with WFE and SEV between the cores (item 22). The 8-region MPU (`mcus/rp2040.hocon:9`) is not modelled. | `v6m/*_tests`, `v6m/exceptions.sysl` (WFE/SEV) | S (MPU, PMSAv7 subset) | — |
| NVIC and SysTick (M0+) | 2.3.2 Interrupts, 2.4 | full | `v6m/except.sysl`, `v6m/nvic.sysl`. 26 IRQ lines; the chip drives TIMER 0–3, IO_IRQ_BANK0 (13), SIO_IRQ_PROC0/1 (15, 16, each into its own core), UART0/1 (20, 21) and RTC (25). | `v6m/nvic_tests`, `chip/pico_tests.sysl`, `chip/pico_xip_tests.sysl` | — | — |
| SIO: CPUID, FIFOs (8-deep), spinlocks, FIFO IRQ | 2.3.1 SIO | full | The RP2350's `Sio` with its depth a parameter (`sio_rp2040`), at the RP2040's offsets (`Rp2040.sio_read`). | `chip/pico_tests.sysl` | — | — |
| SIO GPIO (OUT/OE/IN, SET/CLR/XOR) | 2.3.1 SIO | full | GPIO 0–29 at the RP2040's offsets, mapped onto the shared model's. Of the GPIO_HI bank (the QSPI pins as levels), GPIO_HI_IN answers SS, the flash's chip select, for the ROM's BOOTSEL read (item 23), with the other five pins reading 0; the rest of the bank is refused by name. | `chip/pico_tests.sysl` | — | — |
| SIO hardware divider (per core) | 2.3.1.5 Integer divider | full | `peripheral/rp2040/divider.sysl:5–84`: 8-cycle completion, DIRTY, signed and unsigned. Each core's at SIO 0x060–0x078 (item 22). | `rp2040/tests.sysl`, `chip/pico_tests.sysl` (the bus, and the SDK's sequence on a core) | — | — |
| SIO interpolators (with the RP2040's blend and clamp modes) | 2.3.1.6 Interpolator | full | `peripheral/rp2040/interp.sysl`, the RP2350's model with SHIFT a logical right shift (item 15), each core's pair on the RP2040's SIO (item 22). | `rp2040/interp_tests.sysl` (both shifts) | — | — |
| Bus fabric, BUSCTRL, performance counters | 2.1 Bus fabric | missing | — | — | S | — |
| DMA: 12 channels, DREQs, pacing timers, chaining, ring, sniff/CRC | 2.5 DMA | missing | — | — | L (shared with RP2350, which has more channels and security) | DREQs |
| ROM: contents, float/double/mem tables, `rom_func_lookup`, core-1 launch | 2.8 Bootrom | full | benchtop's own, written in sysl to work as the RP2040's does (`chip/rom/rp2040/`, item 23), at 0; everything runs as its code: core 1's launch wait, core 0's boot path, `rom_table_lookup`, the soft float and double tables (IEEE, denormals flushed as documented), the bit and mem functions, the flash functions through the SSI, `reset_usb_boot` up to its watchdog reboot. The SDK's defaults use it. Refused by name through the ROM's doorbell: USB boot, the tables' flags comparison, the lookup codes 'DT', 'DE', 'WV', 'FS', 'FE'. | `chip/pico_tests.sysl`, `chip/rom40_float_tests.sysl` | — | — |
| Boot sequence (boot2, CRC check, USB boot) | 2.7 Boot sequence | partial | An image that starts in flash boots through the ROM (item 23): core 0 from reset, the BOOTSEL read (SS through GPIO_HI_IN, never pressed), the QSPI pins and pads out of reset, the flash's exit from XIP, boot2 read with the serial 03h command and its CRC checked over the first 252 bytes, boot2, the image; the watchdog's reset runs it again, or a SCRATCH4–7 vector. Also the debugger entry: an ELF into SRAM, core 0 at its entry (item 22); a RAM UF2 entered at its first byte as the ROM enters one (item 24). USB boot (no valid boot2, or `reset_usb_boot`) is refused by name. | `chip/pico_tests.sysl`, `chip/pico_xip_tests.sysl` | M (USB boot) | USB |
| SRAM (264 KB, striped, plus non-striped aliases at 0x2100_0000) | 2.6.2 SRAM | full | 264 KB at 0x2000_0000 on the cores' fast path; the non-striped aliases of banks 0–3 through the bus, word w of bank b at striped word 4w + b (item 22). | `chip/pico_tests.sysl` | — | — |
| XIP, XIP cache, XIP_CTRL, streaming | 2.6.3 Flash | full | The Pico's W25Q16JV, 2 MB (`parts/qspi_flash`, shared with the RP2350), behind the cached, noalloc, nocache and nocache_noalloc aliases, each 16 MB repeating it; read where the SSI is set for XIP, on the cores' fast path while it is (item 24). XIP_CTRL's CTRL (POWER_DOWN disabling the cache), FLUSH, STAT and the streaming FIFO; a write to an uncached alias a bus fault under ERR_BADWRITE, dropped without. The cache as 16 KB of SRAM at 0x1500_0000 while disabled, on the fast path. The cache is functional: no read is stale, CTR_HIT/CTR_ACC refused. | `chip/pico_xip_tests.sysl` (`ssi_flash`) | — | — |
| SSI (DW_apb_ssi) | 4.10 SSI | partial | `peripheral/rp2040/ssi.sysl` (item 24): every register; transfers timed at SCKDV clk_sys cycles a clock, lazily, so chip select stays low while the TX FIFO feeds a command; transmit-and-receive and transmit-only on one lane, the EEPROM read on one, two or four lanes (instruction, address, wait cycles, NDF + 1 frames); chip select from IO_QSPI's SS override; XIP reads honoured exactly where the configuration reads the flash (a copy of it tried over a pattern), boot2_w25q080's quad continuous read and boot2_generic_03h's alike. Refused by name: receive-only, TI/Microwire, loopback, DDR, DMA, the interrupt (XIP_IRQ), either FIFO's overflow or underflow. | `chip/pico_xip_tests.sysl` | S (interrupt, DMA) | DMA |
| VREG_AND_CHIP_RESET | 2.10 Core supply regulator, 2.12 Chip-level reset | full | `peripheral/rp2040/rtc.sysl` `Vreg` (item 24): VREG's VSEL and BOD stored, ROK; CHIP_RESET reporting the power-on reset (HAD_POR), the one reset path benchtop makes. Turning the regulator off or into high impedance is refused by name. | `chip/pico_xip_tests.sysl` | — | — |
| PSM | 2.13 Power-on state machine | full | The RP2350's `Psm` over 17 stages (`psm_rp2040`). Only PROC1 can be forced off, as on the RP2350; its release starts core 1 in the ROM's wait (item 22). | `chip/pico_tests.sysl` | — | — |
| RESETS (25 outputs) | 2.14 Subsystem resets | full | `peripheral/rp2040/resets.sysl:7–55` on the `virt*` boards (`virt/rp.sysl:19–24`). | `rp2040/tests.sysl`, `virt/rp_tests.sysl:246,268` | — | — |
| CLOCKS (no TICKS; clk_rtc instead of clk_hstx) | 2.15 Clocks | partial | `ClocksRp2040` (`peripheral/rp2350/clocks_rp2040.sysl`, item 22): the RP2040's generators, 24.8 and two-bit dividers, clk_peri with none, FC0 from its source table; the watchdog's TICK makes the 1 µs tick. The resus and GPOUT are stored, as on the RP2350. | `chip/pico_tests.sysl` | — | — |
| XOSC | 2.16 Crystal oscillator | partial | The RP2350's `Xosc`, COUNT at the RP2040's 0x1C; DORMANT refused by name. | `chip/pico_tests.sysl` | — | — |
| ROSC (RANDOMBIT) | 2.17 Ring oscillator | full | The RP2350's `Rosc` at the RP2040's offsets (no RANDOM), at a nominal 6.5 MHz (Q8). | `chip/pico_tests.sysl` | — | — |
| PLL_SYS, PLL_USB | 2.18 PLL | full | The RP2350's `Pll`, without its interrupt registers. | `chip/pico_tests.sysl` (125 MHz from the SDK's start-up) | — | — |
| IO_BANK0, PADS_BANK0 (30 GPIOs, no ISO bit) | 2.19 GPIO | full | The RP2350's models with a GPIO count, CTRL's overrides at the RP2040's bits, no IRQSUMMARY, pads with no ISO (reset 0x56); the RP2040's function table, each unmodelled function refused by name (item 22). | `chip/pico_tests.sysl` | — | — |
| IO_QSPI, PADS_QSPI | 2.19 GPIO | partial (stored) | The RP2350's models in the RP2040's shape (`io_qspi_rp2040`, `qspi_pads_rp2040`, item 24): six pins, CTRL's bits at the RP2040's places, INTR at 0x30, pads with no ISO. SS's OUTOVER is the flash's chip select, and a transfer needs every pin's FUNCSEL on the SSI; OUTOVER invert and each STATUS refused by name. | `chip/pico_xip_tests.sysl` | S (levels, BOOTSEL) | — |
| GPIO interrupts (PROC0/1, DORMANT_WAKE) | 2.19 GPIO | full | `GpioIrq` at 30 GPIOs from INTR0 at 0xF0, IO_IRQ_BANK0 (13) into each core; the dormant wake wakes a dormant ROSC (item 22). | `rp2350/gpio_irq_tests.sysl`, `chip/pico_tests.sysl` | — | — |
| SYSINFO | 2.20 Sysinfo | full | CHIP_ID 0x20002927 (B2), PLATFORM ASIC, GITREF 0; read-only (item 22). | `chip/pico_tests.sysl` | — | — |
| SYSCFG | 2.21 Syscfg | register-stub | PROC0_NMI_MASK and PROC1_NMI_MASK stored, ungated (item 23); nothing raises an NMI. The rest refused by name. | `chip/pico_tests.sysl` | S | — |
| TBMAN | 2.22 TBMAN | register-stub | PLATFORM reads ASIC, ungated (item 23); the rest refused by name. | `chip/pico_tests.sysl` | — | — |
| PIO0/1 | 3 PIO | missing | The same machine as the RP2350's (`peripheral/rp2350/pio.sysl`, item 13), with two blocks and no RP2350 additions: PREV and NEXT name the one other block, WAIT IRQ's bit 4 is REL, only flags 0–3 reach INTR, and MOV PINDIRS, the RX entries and JMPPIN are reserved. | — | (shared) | mux, DMA |
| USB controller (device and host) | 4.1 USB | missing | The same controller as the RP2350's. SIE_CTRL alone is stored, ungated, for the SDK's `runtime_init_usb_power_down`; every other register is refused by name (item 22). Its 4 KB of DPRAM is memory, the top of which is core 1's stack while it waits in the boot ROM (item 23). | `chip/pico_tests.sysl` | (shared) | — |
| UART0/1 (PL011) | 4.2 UART | partial | The RP2350's `Uart`, placed (item 22): frames timed from clk_peri through `next_event`, its TX on the pins through the mux, IRQs 20 and 21. The workbench's wires and frames sent in from outside (`Rp2350.wire`, `line_in`) are not on the RP2040 yet. | `chip/pico_tests.sysl` (hello_serial) | S (wires) | — |
| I2C0/1 | 4.3 I2C | missing | Shared. | — | (shared) | — |
| SPI0/1 (PL022) | 4.4 SPI | missing | The model exists (`peripheral/spi/spi.sysl`). | — | S (place) | chip layer |
| PWM (8 slices) | 4.5 PWM | missing | Shared. | — | (shared) | — |
| TIMER | 4.6 Timer | full | `peripheral/rp2040/timer.sysl:5–120`: latch, 4 alarms, pause, debug pause. | `rp2040/tests.sysl`, `virt/rp_tests.sysl` (13) | — | — |
| WATCHDOG | 4.7 Watchdog | full | The tick generator and the counter shared with the RP2350 (`peripheral/rp2040/watchdog.sysl`), TIME falling two a tick (erratum RP2040-E1): CTRL, LOAD, TRIGGER, REASON, SCRATCH0–7. On the `virt*` boards the reset stops the cores at `Reset`, named, since they have no PSM. On the Pico its reset takes down the PSM stages WDSEL selects and the cores boot again through the ROM (item 23); a reset into the ROM's USB boot is refused by name. | `rp2040/tests.sysl`, `rp2040/watchdog_tests.sysl`, `virt/rp_tests.sysl`, `chip/pico_tests.sysl` | — | — |
| RTC | 4.8 RTC | full | `peripheral/rp2040/rtc.sysl` (item 24): the calendar counting clk_rtc over CLKDIV_M1 + 1, with the RP2040's leap-year rule and FORCE_NOTLEAPYEAR; SETUP and LOAD; RTC_0 latching RTC_1; the alarm's masks, taken as the time changes, held until MATCH_ENA clears; INTE/INTF/INTS and IRQ 25. The next second is scheduled only while the alarm is armed. Refused by name: a LOAD of a date the calendar does not hold, CLKDIV_M1 changed while running. | `chip/pico_xip_tests.sysl` (`hello_rtc`, `rtc_alarm`) | — | — |
| ADC and temperature sensor | 4.9 ADC | missing | `rp-adc` has `modelled = false`. | — | (shared) | — |

## 3. Counts

| chip | full | partial | register-stub | missing | rows |
|---|---|---|---|---|---|
| RP2350 | 34 | 8 | 0 | 16 | 58 |
| RP2040 | 20 | 7 | 2 | 8 | 37 |

No RP2350 block is a register-stub today. Every block that is not modelled is refused by the bus, which is
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

   The DCP followed as soon as a program printed a `%f` (item 7, done).
2. **The blocks `runtime_init` polls.** RESETS, TICKS, PLLs and TIMER are done. PSM (core-1 reset),
   ROSC, BOOTRAM (boot locks), SYSINFO, SYSCFG, the POWMAN/VREG store and FC0 are not. Each one
   missing is a refusal at start-up. The census in item 1 below lists them from a real run instead of
   from memory.
3. **UART/stdio.** `hello_serial` and every `printf` go through UART0, at the baud rate, with DREQs
   for the DMA (item 8, done).
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
| Q3 | **Secure-only** first; Non-secure state then built as item 31 |
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
| 6 | **Boot ROM function table**: per Q1, Raspberry Pi's real ROM, mapped on both boards; core 1's wait and every ROM function run as its code. The core-0 boot path is item 30's. **Done** (below). | L | 4, 5 |
| 7 | **DCP (CP4/5)**: the double coprocessor. `pico_float_test` and a `printf("%f")` program are its oracles. **Done** (below). | M | 3 |
| 8 | **UART completion**: baud-timed bytes from clk_peri, both UARTs' pins correct, RT timeout, FIFO-level IRQs, break and errors, DREQ outputs. **Done** (below). | M | 6 |
| 9 | **GPIO function mux and GPIO interrupts**: one routing layer where every producer offers level and OE, IO_IRQ_BANK0, and per-core INTE/INTF/INTS. `gpio/hello_gpio_irq` is its oracle. **Done** (below). | M | 6 |
| 10 | **Watchdog counter and chip reset path**: countdown, TRIGGER, REASON, SCRATCH surviving, and the machine restarting through the boot path. **Done** (below). | M | 5 |
| 11 | **DMA core**: channels, DREQ inputs, pacing timers, chaining, ring, IRQs. The register model is shared by both chips' channel counts. | L | 9 |
| 12 | **DMA sniff/CRC and security attributes**. | S | 11 |
| 13 | **PIO state machine**: the instruction set, FIFOs, shift registers, side-set, divider, IRQ flags, and the RP2350 additions, with register-level tests. **Done** (below). | L | 9 |
| 14 | **PIO integration**: pins through the mux, DREQs, the IRQ lines. `ws2812`, `pio_blink`, `uart_tx` and `uart_rx` are its oracles. | M | 11, 13 |
| 15 | **Doorbells and interpolators** (shared by both chips). **Done** (below). | M | 1 |
| 16 | **SPI on the chip**: the PL022 placed, with DREQs. | S | 9, 11 |
| 17 | **I²C (DW_apb_i2c) master and slave**. | M | 9, 11 |
| 18 | **PWM** (12 slices, IRQ, DREQ, pins through the mux). | M | 9, 11 |
| 19 | **ADC and temperature sensor** (round robin, FIFO, DREQ). | M | 11 |
| 20 | **USB device controller**: endpoints, buffers, IRQ. **Done** (below). | L | 6 |
| 21 | **USB host enumerator and CDC bridge to a pty**. `hello_world/usb` is its oracle. **Done** (below). | M | 20 |
| 22 | **RP2040 chip layer**: `chip.Rp2040` over the shared models (CLOCKS without TICKS, pads without ISO, SIO with 8-deep FIFOs and the divider wired), and the `pico` board running. **Done** (below). | M | 5, 8, 9 |
| 23 | **RP2040 ROM**, following Q1: the float, double and mem tables plus the lookup. | L | 22 |
| 24 | **RP2040 SSI, VREG_AND_CHIP_RESET and RTC**. **Done** (below). | M | 22 |
| 25 | **TRNG and SHA-256**. | S | 11 |
| 26 | **OTP** (a stored array with ROM access; beyond that per Q10). **Done** (below). | M | 6 |
| 27 | **POWMAN AON timer and LPOSC**. **Done** (below). | M | 5 |
| 28 | **BUSCTRL performance counters, TBMAN, GLITCH_DETECTOR stores**. | S | 11 |
| 29 | **HSTX and the SIO TMDS encoder** (per Q9). | M | 11 |
| 30 | **Boot path** (IMAGE_DEF and partitions; per Q2). | L | 6, 25, 26 |
| 31 | **Non-secure TrustZone** (Q3 reopened). **Done** (below). | L | 2 |
| 32 | **USB host mode** (per Q4). | L | 20 |
| 33 | **CYW43439 over PIO gSPI** (DESIGN.md §7 "Later"; the `pico2w` LED). | L | 14 |
| 34 | **POWMAN power states**: STATE, PWRUP0–3, CURRENT_PWRUP_REQ, LAST_SWCORE_PWRUP and BOOTDIS, the P1.x time skip (Q7) and the power-up boot as far as it needs no ROM. **Done** (below). | M | 27 |

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
| 8 | `rom_func_lookup` | ROM halfword at 0x16, then a call into the ROM | not on the bus | Raspberry Pi's A4 ROM, read-only at 0 (`chip/rom/`): its lookup and `bootrom_state_reset`, its own code | 6 (done) |
| 9 | `runtime_init_per_core_enable_coprocessors` | CPACR ← CP0, CP4, CP10 | CP0 and CP4 read as zero | as 1 | 3, 7 |
| 10 | `runtime_init_per_core_enable_coprocessors` | `mrc p4, #0, r0, c0, c0, #1`, the DCP's RCMP | undefined instruction | the DCP in full (`chip/dcp.sysl`): the read ends any sequence and clears the engaged flag | 7 (done) |
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
  regulator registers drop writes until VREG_CTRL.UNLOCK is set, and UNLOCK never clears. The AON timer is
  item 27's (below); the power states and power-up requests stay refused by name.
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

### Item 7, done: the double-precision coprocessor

**`chip/dcp.sysl` runs every instruction of `hardware/dcp_instr.inc.S`**: the 17 `MCRR` writes, the 10
`CDP` operations, and the 10 `MRC` and 14 `MRRC` reads, each in its `R…` and `P…` (peek) form, on CP4
(the core's Secure instance) and CP5 (its Non-secure instance, reached directly from Secure code):
four states, two per core. The registers are the datasheet's (3.6.2.3): `xm`, `ym` (64 bits), `xe`,
`ye` (14), `xf`, `yf` (sign, zero, infinity, NaN) and the 9-bit status (alignment shift, |X| against
|Y|, engaged).

- **The datasheet outlines each instruction (tables 114–116); the canned sequences of
  `dcp_canned.inc.S` fix the formats.** Unpacked mantissas are Q62 under `01`; `RXMS`/`RYMS` read
  Q62−s; `RXYH` the top words, Q30; `RYMR` a reciprocal estimate and `ym` Q31; `RXMQ` a reciprocal
  square-root estimate and `xm`'s top word; `WXMO`/`WXFM` take a product Q60, `WXDD` a quotient Q57,
  `WXFD` one Q61 (the word written twice is its own sticky bit), `WXDQ` a root Q60 and `WXFQ` one Q61.
  `RCMP` is N engaged, Z equal, C greater or equal, V unordered; `RXVD` and `REFD` put the flags at
  bits 28–31, the layout `double_fma_dcp.S` reads them from.
- **Arithmetic is IEEE 754, round to nearest even, with denormals flushed**: an operand whose exponent
  field is 0 is a zero, and a result IEEE gives as a denormal is a zero of its sign. NaN results are
  the default NaN (no payload: the flags hold none). The conversions truncate (`NTDC`) or round to
  nearest even (`NRDC`), saturate, and take a NaN to 0.
- **What the datasheet does not publish is benchtop's own**: the two estimate tables (9 bits each,
  `recip_estimate`, `rsqrt_estimate`), so only `ddiv_fast`, `sqrt_fast` and the single-precision
  `_fast` forms, which do not round correctly, can differ from a chip's in their last bit; the
  version `RXVD` reads (0); the status bits under `RCMP`'s flags; and the unused bits of `REFD`.
- **Refused by name**: `MCR`, the `…2` writes and operations, a `CDP` with CRd or CRn not c0, and any
  opcode or register outside the tables.
- **The engaged flag** is set by every write, operation and middle read, cleared by the reads that end
  a sequence and by `RYMD`, and left by the peeks, `RXMD` and `REFD`; `WEFD` restores it.

**Tests.** `chip/dcp_tests.sysl` writes the core's half of every canned sequence out (the long
multiplies, `SMMLAR`/`SMMLSR` Newton steps and rounding fix-ups) and checks each against the host's
IEEE arithmetic bit for bit over the edges and 20,000 pseudo-random operand pairs: `dadd`, `dsub`,
`dmul`, `ddiv`, `dsqrt`, the comparisons, and the single-precision, `fxprod` and conversion
sequences; the `_fast` forms within an ulp. A mutation of the tie rule or a 4-bit reciprocal table
turns them red. Register-level tests cover each instruction's layout, the engaged flag in both forms,
a save and restore around an interrupting operation, `NORM`, `SQR0`, the estimates' accuracy over
every interval, the special values, the four instances and the refusals; `chip/arm_tests.sysl` runs
the instructions from the core under both executors (flags through `MRC APSR_nzcv`, CP5, CPACR's NOCP).

`examples/pico2/dcp.c` (with `dcp_ops.S`) runs pico_double — C's `+ - * /`, `sqrt`, `fma`, the
comparisons and casts, `ddiv_fast`, `sqrt_fast` — and every canned sequence over 3000 operand sets,
printing one hash per operation, which the test recomputes on the host; core 1 computes alongside, and
a 20 µs timer interrupts core 0 with double arithmetic of its own, some of it inside a sequence, so the
SDK's engaged-flag save and restore runs. Then newlib's `printf` prints `%f`, `%.17g`, `%e` and `%g`
digit for digit as the host's does (the SDK's own printf stops at nine digits, so the program uses
newlib's). pico_double's `fma` takes its "mla path" when an operand is zero, infinite or NaN — the
DCP's product, flushed, then the sum — so `-tiny × tiny + 0` is +0 there; the test models that path.

Core 1's entry returns into the boot ROM's `core1_launch`, which called it, and waits in its
`native_wait_rescue`: `dcp.c`'s core 1 function returns.

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
`connect_internal_flash`, `flash_exit_xip`, `flash_range_*` and `flash_flush_cache`, which run as the
real ROM's code (item 6).

### Item 10, done: the watchdog counter and the chip reset path

**The counter** is one model for both chips (`peripheral/rp2040/watchdog.sysl`, `WatchdogCounter`), at
the offsets `hardware/regs/watchdog.h` gives both: CTRL (ENABLE, PAUSE_DBG0/DBG1/JTAG stored; TIME
read-only; TRIGGER self-clearing, reads 0), LOAD (write-only, 24 bits, reads 0; an alias applies to
0), REASON (TIMER or FORCE, alone; power-on clears it) and SCRATCH0–7. TIME falls once per tick on the
RP2350 (TICKS' watchdog generator) and twice on the RP2040 (its own TICK, erratum RP2040-E1, the
SDK's `WATCHDOG_XFACTOR`). The tick that takes TIME to 0 fires it; a counter enabled at 0 fires on its
first tick; TRIGGER fires it at once, enabled or not. Firing resets the counter's own CTRL to its
power-on value (an assumption: the headers say only that SCRATCH persists, and the SDK never disables
a watchdog left enabled), so TIME stops at 0 and it never fires twice. Each PAUSE bit stops it while
its debug state (`HALTED_CORE0/1`, `HALTED_JTAG`) holds; no board has a debugger yet, so they pass none.

**The RP2350's reset** (`Rp2350.watchdog_reset`, datasheet 7.4 and 12.9): each PSM stage WDSEL selects
goes back to power-on, one by one, not cascading (the SDK selects all but ROSC and XOSC and relies on
those two surviving): ROSC, XOSC, RESETS (every peripheral held and reset), CLOCKS with TICKS (an
assumption: TICKS has no stage of its own), BOOTRAM's locks and WRITE_ONCE (its words kept), XIP and
QMI (the flash part kept), SIO, ACCESSCTRL; PROC0 and PROC1 go to the board. Where RESETS' stage is not
selected, RESETS.WDSEL's peripherals go into reset instead. Kept: REASON and SCRATCH, POWMAN, PSM's own
registers, the SRAM's contents. WDSEL 0 resets nothing and still records REASON. The expiry is a
device event (`next_event`), so a board whose cores sleep wakes for it.

**The Arm board's restart** (`ArmBoard.restart`): a core whose stage was reset stops at `Stop.Reset`
before its next access (after the store, for TRIGGER), under both executors; between quanta core 1
goes back to the ROM wait and core 0 boots as the ROM boots it after a watchdog reset. Modelled at the
board, with the SDK's `watchdog_reboot` and crt0 as the spec: SCRATCH4 = 0xb007c0d3 and SCRATCH5 =
SCRATCH7 ^ -0xb007c0d3 is a PC/SP request (SCRATCH7, SCRATCH6), consumed (SCRATCH4 cleared — an
assumption); otherwise the normal boot hands over the flash image through its vector table at
XIP_BASE, as a flash UF2 at power-on. **Refused by name**: a request whose SCRATCH7 is the magic (a
typed boot, SCRATCH6, e.g. crt0's RAM-image boot — the ROM's boot path, items 6 and 30), and a normal
boot of a flash with no block marker 0xffffded3 in its first 4 KB (BOOTSEL's USB boot). The ROM gains
`get_sys_info` answering BOOT_INFO only, from words the board writes: NORMAL or BOOTSEL at power-on
(by whether the flash holds an image), NORMAL or PC_SP after a watchdog reset — which is what the
RP2350's `watchdog_caused_reboot` reads. The RISC-V board refuses a reset of a hart's stage by name
(Q13); the RP2040 `virt*` boards, which have no PSM, stop the cores at `Reset` with the watchdog's base
as `stop_at` and say so.

`examples/pico2/watchdog.c` (a flash image) runs four boots: fed three times and starved (the reset
100 ms after the last feed, REASON 0x1, `watchdog_caused_reboot` and `watchdog_enable_caused_reboot`
yes), `watchdog_reboot(0, 0, 0)` (REASON 0x2, enable-caused no), and a PC/SP `watchdog_reboot` into a
function that records the boot type (PC_SP) and `watchdog_caused_reboot` (no) before a breakpoint.

### Item 27, done: the always-on timer and the LPOSC

`peripheral/rp2350/powman.sysl`, against `hardware/regs/powman.h` and the SDK's `hardware_powman` and
`pico_aon_timer`; the chip advances it with clk_sys and asks it for the next alarm (`chip/rp2350.sysl`).

- **The count**: 64 bits of milliseconds. A rising TIMER.RUN loads SET_TIME_63TO48..15TO0 (so a stop
  then a start resumes from the last set time, as `powman_timer_stop` documents); RUN cleared holds it;
  CLEAR zeroes it at any time, running or not. READ_TIME_UPPER and LOWER are the live count, so a read
  of UPPER, LOWER, UPPER that straddles the carry is seen and re-read by `powman_timer_get_ms`. 2^64 ms
  wraps to 0.
- **The tick**: USE_LPOSC (the source at power-on), USE_XOSC and USE_GPIO_1KHZ are self-clearing selects;
  USING_* report the source while RUN is set. From the LPOSC or the crystal a millisecond is the stated
  frequency (LPOSC_FREQ_KHZ / XOSC_FREQ_KHZ, kHz with 16 fractional bits) of real source cycles, carried
  exactly in fixed point: at the reset values the crystal (12000 kHz stated, 12 MHz real) is exact and the
  LPOSC (32.768005 kHz stated, 32.768 kHz real) runs 0.2 ppm slow. A stated 0 stops the count; the crystal
  counts only while XOSC is stable. From the GPIO EXT_TIME_REF selects (12, 20, 14, 22) each rising edge
  is one millisecond, which is what `powman_timer_set_1khz_tick_source_gpio` assumes of a 1 kHz clock (it
  sets no frequency). USE_GPIO_1HZ holds the count at the second's 999th millisecond until the 1 Hz edge
  and moves it to the next whole second on the edge. The GPIO is sampled once per chip advance (a board
  quantum), ample for 1 kHz.
- **The alarm**: while ALARM_ENAB is set and the count is at or past ALARM_TIME, TIMER.ALARM latches; it is
  INTR.TIMER and, through INTE, POWMAN_IRQ_TIMER (45). Cleared while still enabled and past, it latches
  again (the warning on `powman_clear_alarm`). `next_event` includes the alarm while it counts from the
  LPOSC or the crystal, exact to the clk_sys cycle, so cores asleep in WFI skip straight to it (Q7).
- **The password** covers every timer register, through every alias.
- **Atomic aliases on fields a write acts on** — one rule for every RP2040 and RP2350 model
  (`peripheral/rp2040/access.sysl`, `w1c` and `triggered`). The datasheet's "Atomic register access" has
  the peripheral perform SET, CLR or XOR in place on the bus data, which arrives unchanged (an alias write
  to POWMAN or ACCESSCTRL must carry the password). A **write-1-to-clear** flag clears on a 1 in the data
  through the plain address and all three aliases: TinyUSB clears USB's SIE_STATUS and BUF_STATUS with
  `usb_hw_clear`, and the SDK clears POWMAN's TIMER.ALARM and STATE.REQ_IGNORED with `powman_clear_bits`
  and SHA-256's ERR_WDATA_NOT_RDY with `hw_clear_bits` — were the write CLR's result, each would clear
  every other pending flag and never its own. Modelled: POWMAN's BADPASSWD, CHIP_RESET.RESCUE_FLAG,
  TIMER.ALARM and INTR.VREG_OUTPUT_LOW; ROSC's STATUS.BADWRITE; TIMER0/1's ARMED and INTR (and the
  RP2040's TIMER). A **self-clearing** control is a stored bit that falls back to 0, so it fires on a 1
  written plainly or through SET or XOR and not through CLR: POWMAN's TIMER.CLEAR and USE_*,
  ACCESSCTRL's CFGRESET, the watchdog's CTRL.TRIGGER; USB's SIE_CTRL controls read back 0. Unobserved
  by any SDK or TinyUSB code, so inferred from that mechanism: a WC flag through SET or XOR, and an SC
  control through CLR. UART's ICR and DR sit behind the bus interposer, which turns an alias write into
  a read-modify-write, and their aliases are refused.
- **The LPOSC** runs at its nominal 32.768 kHz; TRIM and MODE are stored and change nothing (Q8).
- **Assumptions**, since the headers do not say: a write naming several sources takes the GPIO over the
  crystal over the LPOSC; SET_TIME and the frequency registers written while running take effect at the
  next RUN and at once respectively, where the headers only say not to; the 1 Hz synchronisation is the
  hold-and-jump above. NONSEC_WRITE is stored with no effect (Secure-only); PWRUP_ON_ALARM is item 34's.

`examples/pico2/aon_timer.c` starts the timer from the crystal at a wall-clock time with
`aon_timer_start`, reads it after `sleep_ms(250)`, waits for an alarm 1.5 s ahead in WFI, clears it
enabled and disabled, fires an alarm already past, stops the timer and reads it held, and crosses the
32-bit carry; `chip/aon_tests.sysl` asserts every line.

### Item 6, done: Raspberry Pi's boot ROM

**The image** is the A4 stepping's ROM, `bootrom-combined.bin` from the `A4` release of
github.com/raspberrypi/pico-bootrom-rp2350 (tag `A4`, commit
`c6cdb1711f32c3e34faaebd58618a6d096dbd52e`), SHA-256
`c5f9c48c6539f043c423344a53ceaea3774dd1e4c337af49da741a5d259c84bc`, BSD-3-Clause, unmodified
(`sh/sysl/benchtop/chip/rom/`, its README and LICENSE.TXT). It replaces benchtop's own `rom.S`. The
release's binary rather than a local build: the repository needs GCC 12.2 and the CORE-V RISC-V GCC,
neither on this machine.

**What runs as the ROM's code** (`chip/bootrom.sysl`):

- **Core 1's wait, on both boards.** Each M33 starts from reset at the ROM's vector table: its SAU
  region 7, its MPU set-up and their `TT` checks, then core 1 waits for its salts and for core 0's
  launch on the FIFOs, and jumps to the entry from its `core1_launch`. Hart 1 starts at the Hazard3
  reset vector (0x7dfc), reading and fetching the ROM through the bus, and waits in `h3.block`. PSM's
  release of PROC1 starts core 1 from reset again; the chip no longer emulates the protocol
  (`take_launch` is gone). A stray word in the 1's place is answered 0 and the handshake starts over,
  as the ROM's code has it (the emulation used to echo it).
- **Every function the SDK looks up**: the lookup itself, `get_sys_info`, `bootrom_state_reset`, the
  flash functions (`connect_internal_flash`, `flash_exit_xip`, `flash_range_erase`/`_program`,
  `flash_flush_cache`; `hardware_flash` and its unique ID run end to end) and `reboot` (a PC/SP
  reboot through the watchdog). `examples/pico2/rom.c` calls them; `chip/arm_tests.sysl` asserts
  every line it prints and the bytes it programmed.

**What the ROM needed, modelled now:**

| what | where | model |
|---|---|---|
| `TT`'s IDAU fields (the ROM checks 0x02ce0700 and 0x02c70703 exactly) | `v6m/t2secure.sysl` | the board's one numbered IDAU region: 0x7e00–0x7fff, region 2, Non-secure callable, outranking the SAU's Non-secure |
| MPU registers and `TT`'s MPU fields | `v6m/t2secure.sysl` | stored, 8 regions; MREGION, MRVALID, R, RW from AP; an overlap names no region; not enforced |
| NSACR, SCR, the NS alias's CPACR (0xe002ed88) | `v6m/t2secure.sysl` | stored with their bits; the rest of the alias still stops at `NonSecure` |
| `WFE`, `SEV` (the ROM waits in `WFE`) | `v6m/except.sysl` `hint` | an event register per core where the board sets `Cpu.events`; `WFE` consumes it or sleeps until it, or an interrupt that would wake `WFI`; each core's `SEV` sets the other's (`ArmBoard.event`). Other boards keep both as NOPs |
| PADS_QSPI, IO_QSPI (`flash_exit_xip`, `hardware_flash`'s save and restore) | `peripheral/rp2350/qspi_pins.sysl` | stored, behind their RESETS bits, which the boards' boot releases as the ROM's boot path does; STATUS refused |
| OTP read windows (guarded reads of the flash settings) | `peripheral/rp2350/otp.sysl` | a blank array: 0 through all four windows; writes refused |
| QMI clocks with no chip select, and a wide-instruction frame (`flash_exit_xip`'s exit sequence) | `peripheral/rp2350/xip.sysl` | NOPUSH clocks reach no device; a frame whose instruction is dual or quad width is ignored by the SPI-mode flash |

**Kept, and why.** Core 0's boot path through the ROM is not run: it draws its boot salts from the
TRNG through SHA-256, reads OTP's critical and boot flags, and parses IMAGE_DEF and partition tables
(items 25, 26 and 30). So the debugger entry stays (Q2), and power-on and the watchdog's reset hand an
image over as before, and so does POWMAN's power-up (item 34); the last two through one boot path of
the board's (`rom_boot_path`: POWMAN's BOOT0–3 vector, then the watchdog's, unless BOOTDIS.NOW, which
it clears; else the flash image); the board now leaves the core as the ROM's start-up does (CP7 in CPACR, NSACR and
the NS CPACR, SAU region 7) and writes BOOT_INFO where the ROM's `get_sys_info` reads it (BOOTRAM
0x358 and the reboot parameters at 0x340), so the real `get_sys_info` answers it. The RCP's salts are
PROC_COLD's: a core's own reset keeps them (the ROM salts only an unset RCP), and so does the ROM's
`reboot`, which leaves PROC_COLD out of WDSEL.

**Still refused by name**, the census of what the ROM reaches beyond this: the OTP block's registers
(0x40120000: `otp_access`, `get_sys_info`'s CRITICAL and CPU_INFO), USB (`reset_usb_boot`, nsboot),
TRNG and SHA-256 (core 0's boot path), a typed watchdog boot (SCRATCH7 the magic, e.g. a RAM-image boot), and each IO_QSPI STATUS (the
BOOTSEL button). The partition, image and NS-permission functions (`load_partition_table`,
`chain_image`, `set_ns_api_permission` and the rest) run as the ROM's code but are not exercised
yet (item 30). On the RISC-V board hart 0's boot path (the ROM's Armv8-M emulator) is not run, and a
watchdog reset of a hart still ends the run (Q13).

**Fidelity limits.** The MPU is not enforced. A core asleep in `WFE` with no interrupt enabled is not
signalled until an event wakes it, so a line that rises and falls meanwhile leaves nothing pending in
its NVIC. `SEVONPEND` is stored and does nothing. The boot salts the board's hand-off sets are fixed
values, not the ROM's TRNG-derived ones. A RISC-V hart runs ROM code through the interpreter.

**What changed for the programs.** `WFE` now sleeps on the Pico 2, so the SDK's `sleep_ms` sleeps to
its alarm instead of spinning: the same output, sooner, and `aon_timer.c`'s carry line reads `lower 8
after 25000 us` (woken exactly at the alarm, just short of the AON timer's 25th millisecond) where the
spin read `lower 9 after 25001 us`. `examples/pico2-riscv/blink.c` now follows each FIFO word with
`h3.unblock`, as the SDK's `__sev` does, which the ROM's `h3.block` wait needs. Every other
`examples/pico2` program prints what it did.

**Cost**, host instructions retired, three runs each against the branch's base: `bench --core
cortex-m33 kernel33.bin` 37.10 G to 37.14 G; `sram_loop.elf` 5.585 G to 5.642 G and `xip_loop.elf`
5.808 G to 5.865 G (about 1%, core 1 now started and asleep in the ROM); `dcp.elf` 1.136 G to 1.108 G
(its waits now sleep). Signalling core 1's NVIC every quantum while it waits cost 8% on the loops,
which is why a core asleep in `WFE` with nothing enabled is not signalled.

### Item 34, done: the power states

`peripheral/rp2350/powman.sysl` against `hardware/regs/powman.h` and the SDK's `hardware_powman` (whose
`powman_set_power_state`, `powman_configure_wakeup_state` and `pico_low_power`'s P-state path are the
oracle for how software drives it); the chip powers the switched core down and up
(`chip/rp2350.sysl`), and the Arm board decides when and boots it again (`chip/arm_board.sysl`).

- **The encoding**: a STATE bit set is a domain down — bit 3 the switched core, 2 the XIP cache, 1 SRAM
  bank 0 (its bottom 256 KB), 0 bank 1 (the rest, SRAM8 and SRAM9 with it), which is how the SDK reads it.
  CURRENT reads 0 from power-on: the header's reset value 0xF is the state before the power-on sequence.
- **Requests** (REQ written plainly, or through an alias that changes it; the SDK's CLR of REQ_IGNORED is
  none): one powering the switched core down while a power-up request is asserted is ignored (REQ
  kept, REQ_IGNORED); one mixing power-ups with power-downs, or keeping the switched core up without the
  XIP cache, is invalid (REQ taken, BAD_SW_REQ, power-ups made); one powering the switched core down
  waits (WAITING) for the processors, and can be changed or cancelled meanwhile, or is cancelled by a
  power-up request asserting (PWRUP_WHILE_WAITING); any other is made at once (CHANGING never reads 1).
  REQ_IGNORED and PWRUP_WHILE_WAITING are write-1-to-clear through any alias and are INTR's bits 2 and
  3, into POWMAN_IRQ_POW. BAD_HW_REQ reads 0: the hardware's only request, the wake-up, powers up only.
- **Power-up requests**: PWRUP0–3 watch a GPIO for a level or an edge (STATUS latches the edge until a
  1 clears it; RAW_STATUS is the level while enabled; a request just enabled latches no edge from the
  level it finds). The alarm requests while PWRUP_ON_ALARM and ALARM_ENAB are set and ALARM has latched.
  CURRENT_PWRUP_REQ and LAST_SWCORE_PWRUP are bit masks: bit 1–4 PWRUP0–3, 6 the alarm, 5 the debugger's
  (never asserted: no debugger asks for power), and LAST_SWCORE_PWRUP 0 after a power-on. The chip samples
  the watched GPIOs as time passes while a request is enabled or a power-down waits, and before every
  POWMAN access, so a steady state with none enabled pays one flag test per advance.
- **Powering down** (Q7): the board makes a waiting power-down once every started core sleeps with
  nothing pending that would wake it now (a core that would, wakes and runs on). Everything in the
  switched core goes back to power-on; POWMAN, the flash part and what drives the pins from outside are
  kept. The SRAM banks and the XIP cache the state switches off read 0 when they come back (code decoded
  from them decodes again). BOOTDIS.NEXT moves into NOW. While it is down only POWMAN runs: its timer
  from the LPOSC (the crystal is off, so a timer left on it stops), and its requests; the board skips to
  the alarm, or the run ends asleep until something outside drives a power-up GPIO
  (`Rp2350.drive_from_outside`; `benchtop run` says the switched core is powered down).
- **The pads' isolation latches** hold each pad as it was at the power-down: a pad that drove keeps its
  level, one that did not follows what drives it from outside (else the level its pulls gave it), and one
  whose input was off reads low to a power-up request.
- **Powering up**: the switched core and XIP cache come up, a bank comes up unless SEQ_CFG.HW_PWRUP_SRAMn
  keeps it as it slept; REQ takes the new state, LAST_SWCORE_PWRUP the requests, CHIP_RESET's HAD_ bits
  HAD_SWCORE_PD alone (DOUBLE_TAP and RESCUE_FLAG kept). Core 1 starts from reset in the boot ROM; core 0
  boots as the ROM's boot path does after any reset (`ArmBoard.rom_boot_path`, the ROM's steps 7 to 9):
  unless BOOTDIS.NOW, BOOT0–3 and then the watchdog's SCRATCH4–7 are tried as a boot vector (magic,
  check, SP, PC: a PC/SP boot, the vector consumed, BOOT_INFO PC_SP); BOOTDIS.NOW is cleared; otherwise
  the normal boot of the flash image, with BOOT_INFO NORMAL. Words that are no vector (what
  `pico_low_power` writes) are ignored.
- **Where this meets the boot ROM (item 6)**: core 0's boot path through the ROM is not run (it needs
  TRNG, SHA-256 and the image parsing: items 25 and 30), so `rom_boot_path` makes its vector and BOOTDIS steps for
  both the watchdog's reset and POWMAN's power-up.
- **Refused by name**: a valid request switching an SRAM bank off under the running switched core (P0
  with a bank off), and a power-up into one (SEQ_CFG keeping a bank off) — the cores reach SRAM on their
  fast path; a PWRUP enabled on a QSPI pin (48–53), whose level is not modelled (30–47, not bonded on the
  QFN-60, and 54–63 are ignored, as the header says of an invalid selection); a typed boot vector (its PC
  the magic, the ROM's own boot path); a blank flash at power-up (BOOTSEL's USB boot); the RISC-V board's
  harts powering down (Q13).
- **Assumptions**, since the headers do not say: the processors have halted when every started core
  sleeps with nothing pending (no SLEEPDEEP needed; `pico_low_power` sets none for a P-state); an ignored
  request keeps the REQ it replaced; a power-up request asserting while the power-down waits cancels it;
  CURRENT_PWRUP_REQ and LAST_SWCORE_PWRUP are bit masks; cleared SRAM reads 0.

`examples/pico2/powman.c` (a flash image) runs three power-ups, counting them in POWMAN's SCRATCH0: P0
with the XIP cache off (`rc -5`, BAD_SW_REQ), P1.7 against an asserted level request on GPIO 15 (`rc
-14`, REQ_IGNORED), then P1.7 until an alarm 1 s ahead on the LPOSC; woken by the alarm (bit 6, a word in
SRAM bank 0 lost), it sets BOOTDIS.NEXT and BOOT0 and sleeps keeping SRAM bank 0 until GPIO 14 rises,
which the test drives from outside; woken by PWRUP0 (bit 1), the word kept, BOOTDIS consumed and BOOT0
ignored, PWRUP0's edge still latched. `chip/power_tests.sysl` asserts every line and that the alarm's
power-up came between 1000 and 1100 ms on the timer.

### Item 9, done: the GPIO function mux and the GPIO interrupts

`peripheral/rp2350/gpio.sysl` (the mux) and `gpio_irq.sysl` (the interrupts) against `hardware/regs/io_bank0.h`
and the SDK's `hardware_gpio`; the chip routes and samples (`chip/rp2350.sysl` `drive`, `gpio_changed`,
`signal`).

- **One routing layer**: FUNCSEL picks a producer (`route`), the producer offers a level and an output
  enable, OUTOVER and OEOVER override them, and the pad decides. Producers today: SIO, NULL, and each
  UART's TX (drives its line: idle high, a frame's bits, a break low; item 8) and RX, on F2 and on F11. A peripheral
  item adds its producer to `route` and drops its refusal.
- **Refused by name** (`unmodelled_function`), when the FUNCSEL is written, through any alias, CTRL kept:
  SPI (item 16), I²C (17), PWM (18), PIO0–2 (13, 14), HSTX (29), JTAG and CoreSight trace (Q5), UART CTS and
  RTS (item 8), CLOCKS GPIN and GPOUT, the QMI's second chip select, USB_MUXING (20), and any FUNCSEL that
  is no function of its GPIO. XOSC's DORMANT is refused by name too: the crystal stopping until a wake is
  not modelled, where the ROSC's DORMANT is.
- **Inputs**: INOVER changes what the functions read (SIO's GPIO_IN); IRQOVER what the interrupts see,
  which STATUS.IRQTOPROC reports. Both read the pad's input, 0 while it is isolated or its input is off.
- **Interrupts**: INTR's LEVEL_LOW and LEVEL_HIGH follow the input; EDGE_LOW and EDGE_HIGH latch until a 1
  clears them, through any alias. Processor 0, processor 1 and the dormant wake each have INTE and INTF and
  read INTS = (INTR | INTF) & INTE; IRQSUMMARY lists the GPIOs asserted for each, all Secure (Q3), the
  Non-secure words 0. Processor k's line is IO_IRQ_BANK0 (21) in core k on both boards; IO_IRQ_BANK0_NS is
  never raised. The dormant wake's line takes the ROSC out of DORMANT, at once if it is already asserted.
- **When edges latch**: the chip samples the inputs whenever one can move, never per cycle: a write to
  IO_BANK0, PADS_BANK0 or the SIO's GPIO registers, a GPIO coprocessor write, a RESETS change of either
  bank, and a level driven from outside (`drive_from_outside`). Nothing latches while RESETS holds
  IO_BANK0, whose reset clears the latches, enables and forces. No producer changes level with time yet,
  so nothing is sampled as time passes.
- **Shared with the RP2040** (item 22): `GpioIrq` takes the GPIO count and INTR0's offset; it is tested at
  both layouts (48 from 0x230, 30 from 0xF0).
- **Assumptions**, since the headers do not say: IRQOVER sits between the pad and the edge detector (so
  an override can latch an edge), and IRQTOPROC is that overridden input.
- **Two board fixes the programs found.** The Arm board slept the rest of a run's budget away when both
  cores were asleep at the end of a quantum though one had been woken by the other's `SEV` during it (the
  boot ROM's launch handshake, when no alarm was pending); now a core that would wake runs the next
  quantum (`ArmBoard.run`). The RISC-V board likewise returned `Asleep` with a hart's interrupt pending
  (a GPIO edge driven between runs); `RiscvBoard.asleep` now asks `riscv.wakes_now`.

The oracles: `examples/pico2/hello_gpio_irq.c` is pico-examples' `gpio/hello_gpio_irq`, identical to
`pico-scratch/pico-examples` at commit 0d62f75 apart from its two-line header comment, run on the Arm board with GPIO 2 driven high, low, high and released;
`examples/pico2/gpio_irq.c` covers both cores, levels, a force, both overrides and the dormant wake with
nothing outside; `examples/pico2-riscv/gpio_irq.c` is its RISC-V twin, through Hazard3's Xh3irq.
`chip/gpio_tests.sysl` asserts each program's output line by line, and the mux, overrides, edge sources,
per-core lines and dormant wake at register level; `rp2350/gpio_irq_tests.sysl` the interrupt registers.

**Cost**, host instructions retired, three interleaved runs each against the branch's base: `sram_loop.elf`
5.644–5.664 G to 5.652–5.660 G, `xip_loop.elf` 5.864–5.872 G to 5.875–5.883 G, `bench --core cortex-m33
kernel33.bin` 37.140–37.143 G to 37.139–37.145 G: within run-to-run noise.

### Item 20, done: the USB device controller

`peripheral/rp2350/usb.sysl` against `hardware/regs/usb.h`, `usb_device_dpram.h` and TinyUSB's RP2350
port (`dcd_rp2040.c`, `rp2040_usb.c`, which is the oracle for how software drives it). It replaces the
SIE_CTRL stub `startup.sysl` held. USBCTRL_DPRAM is placed at 0x50100000 (4 KB, no atomic aliases) beside
the registers at 0x50110000, and USBCTRL_IRQ is IRQ 14.

- **The seam for item 21.** benchtop's host drives the bus between runs, one event or transaction per
  call, on `ArmBoard`: `usb_attach(on)`, `usb_bus_reset()`, `usb_sof(frame)`, `usb_setup(address,
  [8]u8) -> Handshake`, `usb_in(address, ep) -> Result[InReply, string]` (a `Packet` with its PID and up
  to 64 bytes, or NAK, STALL, silence) and `usb_out(address, ep, pid, bytes) -> Result[Handshake,
  string]`. Each is answered by the controller at the chip's present time, and the cores then see the
  interrupt it raised. A host retries a NAK after running the board on, as a host retries in a later
  frame, and waits the 2 ms USB allows after SET_ADDRESS (TinyUSB sets ADDR_ENDP from its task). The
  bus is ideal: full speed, nothing lost, every packet the device sends acknowledged.
- **Connection**: on the bus while a host is attached, the controller is enabled in device mode, the
  PHY is out of isolation and powered, the controller is muxed to it, VBUS is detected (USB_PWR's
  override where enabled, otherwise the host's attachment: an assumption) and the pull-up is on;
  each change latches DEV_CONN_DIS. Only ADDR_ENDP's address is answered.
- **Transactions**: a SETUP lands in DPRAM's first 8 bytes, sets SETUP_REC and clears EP_STALL_ARM. IN
  and OUT reach endpoint 0 or an endpoint whose control word has ENABLE (otherwise silence), NAK under
  EP_ABORT, STALL on buffer control's STALL (endpoint 0 needs its EP_STALL_ARM bit too), and otherwise
  use the endpoint's current buffer: an IN sends an AVAILABLE and FULL buffer and clears both, an OUT
  fills an AVAILABLE buffer, sets LENGTH and FULL and clears AVAILABLE, a buffer not available NAKs.
  INTERRUPT_ON_NAK/STALL (EP0_INT_NAK/STALL) set EP_STATUS_STALL_NAK. BUFF_STATUS follows the
  interrupt-per-buffer and per-two-buffers settings, BUFF_CPU_SHOULD_HANDLE names the buffer, LAST sets
  TRANS_COMPLETE, a short OUT sets RX_SHORT_PACKET and keeps a double-buffered endpoint on its buffer,
  and a buffer control word written with RESET goes back to buffer 0.
- **Assumptions**, since the headers do not say: an OUT with the wrong data PID is acknowledged and
  dropped, with DATA_SEQ_ERROR (the USB specification's rule); a bus reset leaves ADDR_ENDP alone
  (TinyUSB clears it); a SETUP sends endpoint 0 back to buffer 0; EP_ABORT_DONE sets as an EP_ABORT bit
  does, every endpoint being idle between transactions; the controller's reset keeps DPRAM's contents.
  LINE_STATE and SPEED read 0. The host never suspends the bus, so SUSPENDED and the suspend and resume
  interrupts never assert.
- **Refused by name**: host mode (Q4, item 32); remote wake-up (SIE_CTRL.RESUME); SIE_CTRL.DIRECT_EN and
  EP0_STOP_ON_SHORT_PACKET; muxing to anything but the PHY; USBPHY_DIRECT and a nonzero
  USBPHY_DIRECT_OVERRIDE; the SOF timestamps and SM_STATE; DEV_SM_WATCHDOG.ENABLE; and, when a
  transaction reaches them, an isochronous endpoint, a LENGTH over 64, a buffer past DPRAM's end, an IN
  buffer AVAILABLE but not FULL, an OUT buffer AVAILABLE and FULL, and an OUT packet longer than LENGTH.
  The host-mode registers are stored and do nothing.
- **SIE_CTRL powers on at 0x8000** (PULLDOWN_EN), the header's reset value; the stub had 0, so the
  SDK's `runtime_init_usb_power_down`, which sets TRANSCEIVER_PD only where SIE_CTRL holds its reset
  value, did nothing.
- **A write to the boot ROM is now ignored on the Arm board** rather than refused: TinyUSB's port stores
  endpoint 0's control word through a null pointer (`hw_endpoint_start_next_buffer`, whose EP0 has no
  endpoint control register), so every control transfer writes 0x00000000, which every Pico 2 running
  stdio_usb survives. The RISC-V board still refuses one (Q13).

`examples/pico2/usb_device.c` is the SDK's stdio_usb, unmodified, with stdio on UART0 as well.
`chip/usb_tests.sysl` plays the host: attach, bus reset, GET_DESCRIPTOR of the device at address 0,
SET_ADDRESS, the device descriptor again at the new address (address 0 now silent), the configuration
in a 9-byte and an 84-byte read, string descriptors 0 to 4, SET_CONFIGURATION; each compared with the
ELF's own `usbd_desc_device` and `usbd_desc_cfg` and the strings in `stdio_usb_descriptors.c` (the
serial number with the one the program prints). Then SET_CONTROL_LINE_STATE raises DTR, the program's
line arrives on bulk IN 0x82 with alternating PIDs, and a byte written to bulk OUT 0x02 reaches its
`getchar`. The serial number is the OTP's chip ID, 1B970E427A015CD3, through `get_sys_info`'s CHIP_INFO
(item 26).

**Cost**, host instructions retired, three runs each against the branch's base. DPRAM's 4 KB inside
`Rp2350` first cost 3% on the loops and 10% on `dcp.elf` (`sram_loop.elf` 5.645 G to 5.831 G): the
chip's read-only methods took `self` by value, which copies the whole chip, and the hot ones (`sources`,
`sys_hz` and `ref_hz` on every catch-up, `placed` on every device access, `signal`, `next_event`, the
pin and drive queries) and the controller's own (`irq` on every signal) now take `*self`. With that:
`bench --core cortex-m33 kernel33.bin` 37.14 G to 37.14 G; `sram_loop.elf` 5.645 G to 5.201 G and
`xip_loop.elf` 5.868 G to 5.421 G (8% fewer); `dcp.elf` 1.110 G to 0.960 G (14% fewer).

### Item 21, done: the USB host and the CDC bridge

`chip/usb_host.sysl` is the computer a Pico 2 is plugged into, in benchtop's own code: `ArmBoard.attach_usb_host`
puts it on the bus, and the board gives it a frame every millisecond of clk_sys (`serve_usb`, checked at
each quantum's end; `next_wake` includes the next frame, so sleeping cores wake for it). It counts only
while it has frames to make: from the device's pull-up appearing until the port is open or given up on,
then every frame while it runs. A bus with no device on it costs a comparison a quantum and never stops
a run ending `Asleep`.

- **Enumeration**, as Linux and macOS make it: the 100 ms debounce, the bus reset, 10 ms of reset
  recovery and a SOF every frame from then on; GET_DESCRIPTOR of the device for 64 bytes at address 0
  (bMaxPacketSize0 taken from it), SET_ADDRESS 1 and the 2 ms before the device is addressed again, the
  device descriptor at its address, the configuration's 9-byte header and then all wTotalLength bytes.
  `cdc_port` finds the first CDC ACM communication interface (class 2, subclass 2) and the first data
  interface (class 0x0A) with a bulk IN and a bulk OUT endpoint. Then SET_CONFIGURATION and
  SET_CONTROL_LINE_STATE with DTR. A control stage the device NAKs is retried the next frame.
- **Running**: every frame polls bulk IN until a NAK (up to 19 packets, and only while `received` holds
  under 4 KB, as a host's buffer fills), then sends `sending` to bulk OUT 64 bytes a packet; each
  direction checks its data toggle from DATA0.
- **Disconnect and reconnect**: the device leaving the bus (`tud_disconnect`, the chip's reset) is
  forgotten and its return debounced afresh; `request_reset` resets the bus and enumerates again.
- **Named, never hanging**: a device with no CDC ACM interface (with the classes it has), a stalled
  request or status stage, a control stage NAKed for 5 s (Linux's USB_CTRL_GET_TIMEOUT), no answer at
  its address, a data PID out of turn, more data than asked for, a malformed or short descriptor, a
  stalled bulk endpoint. The host gives up (`take_note`; `benchtop run` prints it on stderr), disables
  the port until the device disconnects, and the program runs on. It does not clear a halt, send
  SET_LINE_CODING, poll the notification endpoint, suspend the bus or reach a hub.
- **The bridge** (`benchtop run --usb console|pty|off`, Arm RP2350 boards only, refused by name with
  `--window` or another board). **The default is `console`**: the port's bytes join stdout beside UART0,
  each a line at a time through `show_console`, so neither interleaves inside the other's lines; no
  input reaches the port, as none reaches UART0 in this command. `pty` opens a raw pseudo-terminal
  (`host/pty.sysl`, with `host/__posix__/pty.c` for termios: `posix_openpt`, `cfmakeraw`, non-blocking),
  prints its name on stderr and exchanges bytes with it after every run of the board; benchtop holds
  the terminal side open itself, so a program that opens and closes it does not hang it up. `off`
  leaves the socket empty.

The oracles: pico-examples' `hello_world/usb` (`examples/pico2/hello_usb.c`, a flash image, stdio over
USB only) delivers three "Hello, world!" lines in 3.5 s, the one at power-on dropped as stdio_usb drops
output with no port open; through `./benchtop run` its lines reach a pipe. `usb_device.c`'s line reaches
a program that opened a real pty, and the `x` typed there reaches `getchar` (skipped by name where no pty
can be had). `usb_reconnect.c` (our own) is enumerated twice around `tud_disconnect`/`tud_connect`.
pico-examples' `usb/device/dev_lowlevel` (vendor class 0xFF, the registers driven directly) is enumerated
up to its configuration and given up on by name. At the controller alone (`chip/usb_host_tests.sysl`),
the test plays the firmware: the reset on the 100th frame after the connection and the first SETUP 11
frames later with SOF_RD 10; a stall; 5000 frames of NAK; DATA0 where DATA1 is due; the device leaving
mid-enumeration. Changing the debounce to 50 frames, or inverting the data-stage toggle check, turns
8 of the 15 tests red.

**Cost**, host instructions retired, three interleaved runs each against the branch's base (dev at
51a684d, `/usr/bin/time -l`, `benchtop run --board pico2`, whose host is now on the bus with no device):
`sram_loop.elf` 5.392–5.406 G to 5.401 G, `xip_loop.elf` 5.615 G to 5.623 G, `dcp.elf` 6.380 G to
6.381–6.384 G (+0.1%); `--usb off` on `sram_loop.elf` reads 5.397 G. `bench --core` does not reach the
board and was not rerun. With USB active, `hello_usb.elf` (cores asleep between lines, the host polling
an idle port every frame) ran 218,869 guest seconds in 54 s: 6.3 k host instructions a frame, 4,000
times real time. `usb_device.elf` to its breakpoint (enumeration, then `getchar_timeout_us` spinning
for 2 s) is 32.90 G, the spinning guest's own.

### Item 26, done: the OTP

Q10 decided "OTP with ECC": the array with its ECC, the read windows, the control registers, and access
from Raspberry Pi's boot ROM (`peripheral/rp2350/otp.sysl`, wired in `chip/rp2350.sysl`).

**Persistence: per run, as the flash is.** The array (4096 words, a row in the low 24 bits of each) is
the board's (`ArmBoard.otp_rows`, `RiscvBoard.otp_rows`), connected to the chip as the flash's array is.
Each `boot` starts it from the factory image (`factory_otp`): CHIPID0–3 and RANDID0–7 programmed with
their ECC, benchtop's own fixed values (no real chip's), the rest blank. A chip reset, the watchdog's and
POWMAN's power-up keep it; nothing is written to a file.

**What is modelled:**

| what | model |
|---|---|
| the four windows | OTP_DATA and _GUARDED: 16 bits a row through the ECC, two rows a word; _RAW and _RAW_GUARDED: 24 bits a row; every width and lane |
| the ECC | the datasheet's SECDED: data bit j at the j-th Hamming position that is not a power of two, ECC bit 5 the parity of the rest; one wrong bit of 22 corrected; a row with both BRBP bits set is its codeword inverted |
| SW_LOCK0–63 | a write ORs in (plain, SET, XOR; CLR clears nothing); the OTP's reset loads each from PAGEn_LOCK1 by a 3-way majority of its bytes; a Secure read of an INACCESSIBLE page (or the reserved 2) is a bus fault through a guarded window and reads 0 through the others; the lock rows always read |
| SBPI | as the ROM's `otp_access` drives it: one-byte register writes to the macro's DAP (0x02) and PMC (0x3a); the row in DAP 0xfc/0xfd, its bits in 0xc0/0xc1/0xe0, PMC 0x01 programs (0 to 1 only), the one-byte PMC 0xbf read, PMC 0x02; INSTR_DONE and RDATA_VLD write-one-to-clear; only while USR.DCTRL is clear |
| USR | DCTRL and PD; the windows are the bus error while DCTRL is clear or PD set |
| CRITICAL, KEY_VALID | 0: no critical flag row or key can be programmed |
| ARCHSEL, ARCHSEL_STATUS | the board's architecture (Arm 0, RISC-V 3) |
| the boot path's chip ID | `write_boot_info` copies OTP_DATA's first two words to BOOTRAM 0x3f8, as the ROM's boot path does (ROM 0x39fc), which `get_sys_info`'s CHIP_INFO answers and the SDK's unique board ID (and so the USB serial number) reads |
| the watchdog's reset | OTP's PSM stage (WDSEL bit 1) puts the registers back, reloading SW_LOCK |

The SBPI protocol is the ROM's own: the instruction stream `otp_access` sent (traced once from
`examples/pico2/otp.c`) is what the model decodes; the macro's timing registers are stored and mean
nothing here.

**Refused by name:** any other SBPI instruction; programming a row SW_LOCK leaves not writable (a lock
row by its own page's lock; the ROM checks first and never sends one), a critical flag row (CRIT0, CRIT1:
the boot, debug and core enables are not run), a hardware key or its valid flags, and key locks in a
PAGEn_LOCK0; an uncorrectable row through the unguarded ECC window; one BRBP bit of two; ARCHSEL naming
the other architecture; DBG, BIST, CRT_KEY_W0–3, DEBUGEN, DEBUGEN_LOCK, BOOTDIS, INTR/INTE/INTF/INTS.

**Assumptions**, since the headers do not say: an unguarded read of an inaccessible page reads 0 (the
ROM's `otp_access` reads the row through OTP_DATA_RAW before checking its lock, ROM 0xa60, so silicon
cannot fault there, and the value is discarded); a CLR-alias write to SW_LOCK clears nothing; the 0xbf
status read answers 0, which the ROM takes as success.

**The oracle**, `examples/pico2/otp.c` (built by `build.sh`), through the ROM: the chip ID through
OTP_DATA, `get_sys_info` (CHIP_INFO, CRITICAL, CPU_INFO) and `otp_access`; two user rows written with ECC
and read back through the ROM, the ECC window and the raw window; a raw row; a write clearing a bit (-18,
UNSUPPORTED_MODIFICATION); the codeword of each data bit alone; an ECC write over a row with a bit set,
which the ROM stores inverted with both BRBP bits (c20001); a page SW_LOCK makes read-only (write -4,
read 0) and one it makes inaccessible (both -4); a lock that does not go back; a row past the end (-5)
and an odd length (-11). `chip/otp_tests.sysl` asserts every line, the rows in the array, and each
codeword the ROM's encoder made against benchtop's own `ecc_encode` (built from the construction, not
from the ROM's output; changing the parity rule turns it red). `peripheral/rp2350/otp_tests.sysl` covers
the model register by register.

**Cost**, host instructions retired, three runs each against the branch's base. The array inside each
board (16 KB) and the OTP block's registers inside `Rp2350` first cost 1.5% on the loops and 20% on
`dcp.elf`: the boards' read-only methods took `self` by value, copying the whole board, and the hot ones
(`at` on every device access, `asleep`, `reset_first`, `sram_line`) and the chip's `restarting` and
`power_down_due` now take `*self`; the OTP block keeps only the macro's address and data registers
(`row_at`, `row_bits`) and SW_LOCK as bytes. With that: `bench --core cortex-m33 kernel33.bin` 37.17 G to
37.17 G; `sram_loop.elf` 5.207 G to 5.105 G and `xip_loop.elf` 5.428 G to 5.323 G (2% fewer); `dcp.elf`
0.961 G to 0.694 G (28% fewer).

### Item 8, done: the UARTs

`peripheral/rp2350/uart.sysl` against the PL011 TRM and the field descriptions of `hardware/regs/uart.h`;
the chip times the lines and carries their frames (`chip/rp2350.sysl` `advance_lines`, `deliver`,
`line_in`, `wire`, `uart_write`).

- **Timed lines**: one bit is 16 periods of clk_peri over IBRD + FBRD/64, counted exactly as 64 × IBRD +
  FBRD quarter clk_peri cycles; IBRD and FBRD take effect at the next LCR_H write. A frame is a start bit,
  5 to 8 data bits, the parity bit (even, odd or stuck) and one or two stop bits. The chip hands the UARTs
  clk_peri's time only while a line is busy (a frame, a break, an arriving frame, the receive timeout),
  event by event, and offers the next of those as a `next_event`, so a sleeping board wakes for it and an
  idle UART costs nothing. A frame in progress finishes after UARTEN or TXE is cleared.
- **Pins and wires**: a TX function drives its pin with the line's level (idle high, the frame's bits, low
  through a break), on F2 and F11. `Rp2350.wire(a, b)` puts a wire between two GPIOs on the workbench:
  each sees the other's level where it drives, and a frame sent from a GPIO whose function is a UART's TX
  and whose pad drives reaches the UART whose RX is the other end's function. `line_in(n, frame)` sends a
  frame into GPIO `n` from off the chip, arriving a frame later at that UART's rate (false while the line
  still carries the last). The console is UART0's TX, whatever pin carries it, as before.
- **FIFOs and interrupts**: 32 deep, or a 1-deep holding register without FEN; a character written while
  the UART is off waits in the FIFO (BUSY set, as the TRM says). RX asserts as the receive FIFO reaches
  its IFLS level and clears as reads take it below; TX asserts as the transmit FIFO falls to its level (the
  holding register empties, without FEN) and clears as writes take it above; RT 32 bit periods after the
  last character, with one waiting; ICR clears each. UARTRIS reads 0 at power-on (uart.h), where the old
  model latched TX.
- **Errors and break**: FE, PE and BE ride with their character into UARTDR and UARTRSR and set their
  interrupts; a full FIFO loses the character, sets OE in UARTRSR and the interrupt, and marks the next
  character in with UARTDR.OE; UARTECR clears UARTRSR. LCR_H.BRK holds the line low after the frame in
  progress, and a receiver at the other end takes it as one break character (0 with BE and FE) a frame
  later; no frame starts until BRK clears. CR.LBE feeds the UART's frames and breaks to its own receiver
  and ignores the line from outside.
- **DMA requests**: `Uart.dreq_tx` (TXDMAE and room in the transmit FIFO) and `dreq_rx` (RXDMAE and a
  character waiting, stopped by DMAONERR while an error interrupt is asserted); `Rp2350.uart_dreq(n)`
  answers DREQ 28 to 31 (`DREQ_UART0_TX` on) for item 11.
- **Refused by name**: CR.SIREN (IrDA), CR.CTSEN and CR.RTSEN (hardware flow control: the CTS and RTS
  functions stay refused in the mux, so there is no pin for either), the reserved IFLS levels, a character
  to send with IBRD 0 latched, and a frame its pins cannot show as benchtop models them: one starting on a
  pin, or the far end of its wire, whose GPIO interrupts are enabled (the interrupts do not sample a
  frame's bits), and one going over a wire through OUTOVER or OEOVER. Over a wire, a receiver at another
  divisor, word length or parity, an RX pad that does not read its input, and an INOVER on the RX pin stop
  the run with the reason (`take_line_fault`). Each refused register write leaves the UART as it was.
  Modem status reads 0 in UARTFR.
- **Assumptions**, since the headers do not say: the DREQs are the PL011's single requests; with LBE set
  the frame still drives the TX pin; a break needs UARTEN.
- **Two board changes.** A run that ends at a breakpoint or a fault now hands over what the chip did in
  that last part of a quantum (`settle`): before, those characters reached the console only on the next
  run. `ArmBoard.finish_sending` and `RiscvBoard.finish_sending` let UART0 send what it holds with the
  cores stopped, as a chip runs on while a debugger holds its cores; `benchtop run` does that at a
  breakpoint, so the console still ends with every character the program wrote.
- **What the console now shows differently**: the same text, at the line's pace. The SDK's `printf` waits
  until UART0 has sent each line (`stdio_uart`'s flush polls UARTFR.BUSY), so a program's own clock moves
  on by its characters' time: `aon_timer.elf` now reads 1700000000.252 and 1700000001.754 where it read
  .250 and .750, `hello_serial` puts 100 ms plus its line's 17 characters between lines, and `watchdog.elf`'s
  boot lines come their characters later. Every other `examples/pico2` program prints exactly as before.
- **A finding in `rom.c`**: after the ROM's PC/SP reboot, which resets CLOCKS, `rebooted()` printed through
  a UART whose clk_peri was off; the instant UART let it, and a Pico 2 would have hung there, as the timed
  one now does. It now releases the PLLs and runs `runtime_init_clocks()` first.

The oracles: `examples/pico2/hello_uart.c` and `uart_advanced.c` are pico-examples' `uart/hello_uart` and
`uart/uart_advanced`, identical to `pico-scratch/pico-examples` at commit 0d62f75 apart from their header
comment. `hello_uart` returns from `main` with characters still in the FIFO; `uart_advanced` echoes `HAL`,
sent into GPIO 1, as `IBM`, through its RX interrupt with the FIFOs off. `examples/pico2/uart_loopback.c`
puts UART0 and UART1 on two wires (GPIO 12 to 5, 4 to 13): UART1 hears the console's `ping`, its receive
timeout raised after 804 µs by the 1 µs timer (798.6 µs on the line, the rest printf's own work and the
interrupt taken at the end of a 1000-cycle quantum), `pong` back, a break read as 0x500, and an overrun
of UART0's FIFO. `chip/uart_tests.sysl` asserts each program's output and, at register level, the timing
through `next_event`, a halved clk_peri, the pins bit by bit, the wires and every refusal;
`rp2350/uart_tests.sysl` the model alone.

**Cost**, host instructions retired, three interleaved runs each against the branch's base (`/usr/bin/time
-l`, `benchtop run --board pico2`): `bench --core cortex-m33 kernel33.bin` 120.43–120.45 G to
120.43–120.45 G, unchanged. The Pico 2 programs rise by the guest's own waiting, not by a cost per
instruction: `sram_loop.elf` 5.11–5.12 G to 5.46–5.47 G and `xip_loop.elf` 5.33 G to 5.67–5.68 G (+7%,
their 58 characters now waited for through UARTFR polls), and `dcp.elf` 0.70 G to 6.84 G, whose ~2,000
characters are now 27 ms of guest time spent polling UART0 on both cores where they took none.

### Item 31, done: Non-secure TrustZone

Q3's cut line is lifted: the M33 runs both security states (`v6m/t2nonsecure.sysl`, DESIGN.md §2.7),
against the ARMv8-M ARM:

- **Banked registers** live where the Secure-only core kept them, as the current state's copy, with
  the other state's in `Cpu.banked`; a change of state exchanges them. `MRS`/`MSR` of the `_NS`
  registers (SP_NS as CONTROL_NS.SPSEL selects) reach the bank from Secure state and read zero from
  Non-secure state. The system control space's Non-secure alias is Non-secure state's view: its own
  VTOR, CCR, SCR, CPACR (masked by NSACR), MPU, SHPR, SHCSR, CFSR and a second **SysTick**, counting
  while either state runs; the NVIC registers of the IRQs NVIC_ITNS gives it; SAU, SFSR, SFAR and HFSR
  reading zero. Both states' PRIMASK, BASEPRI and FAULTMASK raise the execution priority.
- **Attribution enforced, free on the fast paths.** Non-secure state runs on records and a page table
  of its own (each the memory's, per core, made on first entry): a page is on the Non-secure table only
  where all of it is Non-secure, and every other access takes the slow path, which makes it where it is
  Non-secure and takes SecureFault AUVIOL (SFAR) where not. Decoding judges the address: INVEP for a
  Secure address from Non-secure state (the entry to Secure state at a Non-secure-callable `SG`),
  INVTRAN for a Non-secure one from Secure state. The SAU and the IDAU (the boot ROM's NSC region)
  combine as the more secure answer. A change to the SAU or to a page's mapping remakes the table and
  forgets the decoded records, through the code-line marks, before the next run.
- **Transitions:** `SG`, `BXNS`, `BLXNS` (return address and partial RETPSR on the Secure stack,
  FNC_RETURN, IPSR 1 from handler mode), FNC_RETURN; exceptions to the other state (S, DCRS, ES exact;
  the callee registers under the integrity signature and R0–R12 cleared from Secure to Non-secure; the
  signature checked on return, INVIS; ES checked, INVER or INVPC); banked exceptions' Non-secure copies
  at `NS_BANK` + n in the NVIC's sets; IRQs by NVIC_ITNS. `VLSTM`/`VLLDM`, which libgcc's
  `__gnu_cmse_nonsecure_call` wraps around BLXNS, are NOPs with no floating-point context active.
- **ACCESSCTRL** gates every core bus access on the Arm board by master, state and privilege
  (FORCE_CORE_NS included): a denial is refused by name as silicon's bus fault. Secure privileged code
  with nothing denied pays two tests.
- **Refused by name:** a change of state with a floating-point context active (lazy preservation,
  SFPA), AIRCR.BFHFNMINS or PRIS set, a Non-secure PRIGROUP other than the Secure one, the FP
  registers' Non-secure view, the PPB alias outside the system control space, the SIO from Non-secure
  state (its Non-secure bank), and an ACCESSCTRL write restricting a memory device. The MPU is stored
  per state and answered by `TT`, not enforced.

`examples/pico2/nonsecure.c` with `nonsecure_ns.S` is the oracle: the SDK-built Secure image gives the
SAU its own Non-secure code, stack and veneer, calls the Non-secure function through a
`cmse_nonsecure_call` pointer (libgcc's BLXNS sequence), which calls back through `SG` and returns with
FNC_RETURN (`ns_function(20) = 142 after 1 call(s)`), prints TT of the three regions, and takes
SecureFault AUVIOL from a Non-secure load of Secure memory in its own handler (`sfsr 00000048`). The
compiler's `cmse_nonsecure_entry` is not used: GNU ld puts its veneers in `.gnu.sgstubs`, which the
SDK's linker script does not place, so the veneer and its BXNS return are hand-written.
`v6m/t2nonsecure_tests.sysl` (20 cases, both executors) covers each transition, fault and refusal;
`chip/arm_tests.sysl` the oracle and ACCESSCTRL. Breaking the integrity check or the callee clearing
turns the INVIS and the IRQ cases red.

**Cost**, host instructions retired, three runs each against the branch's base (dev at b544795):
`bench --core cortex-m33 kernel33.bin` 37.17 G to 36.88 G; `sram_loop.elf` 5.104 G to 5.080 G and
`xip_loop.elf` 5.330 G to 5.300 G. A first cut cost 0.7% on the loops: the SecureFault test grew the
slow path that `load` and `store` inline into every handler; it is out of line now (`slow_load`,
`slow_store`), which is also why the base got faster.

### Item 13, done: the PIO state machines

`peripheral/rp2350/pio.sysl` against the field descriptions of `hardware/regs/pio.h`, the encodings of
`hardware/pio_instructions.h` and the SDK's `pioasm`; the chip places PIO0–2 behind their RESETS bits and
runs them on clk_sys (`chip/rp2350.sysl` `advance`, `pio_drive`, `pio_pins`).

- **Time**: each state machine runs one cycle on each clk_sys cycle its fractional divider fires: a phase
  in 1/256 cycles gains 256 a cycle and fires on reaching 256 × INT + FRAC (INT 0 is 65536), so 2.5 runs
  3, 2, 3, 2 cycles apart and 1.25 on cycles 2, 3, 4, 5, 7. The dividers free-run whether or not their
  state machines do, and CLKDIV_RESTART (and NEXTPREV_CLKDIV_RESTART) puts the phase at 0; a new
  divisor keeps the cycles a state machine has left. The phase is worked out from the time when needed,
  so nothing is counted while no state machine runs. The blocks run to the chip's `now` only when
  something can see them: a PIO, RESETS or ACCESSCTRL access, `pio_drive` and `pio_pins`, and the
  faults the board collects each quantum (`take_line_fault`), so the chip's `advance` pays nothing for
  them. Item 14's pins, DREQs and interrupts will give them events of their own.
- **Only what changes is run**: a state machine is visited on the cycle its delay ends, its next attempt,
  or an autopull refill. One stalled on an empty TX FIFO, a full RX FIFO, a pin or an IRQ flag is not
  visited until a FIFO access, a register write, a level driven onto the inputs or an IRQ flag changes;
  then it tries again on its next cycle, as it would have. A WAIT on a pin wakes on the cycle the
  synchroniser brings the level through.
- **The instruction set**, PIO version 1: JMP's eight conditions (X-- and Y-- decrement either way),
  WAIT on GPIO (relative to GPIOBASE), PIN (the IN mapping and IN_COUNT), IRQ (polarity 1 clears it) and
  JMPPIN + 0–3; IN from every source, either direction, the count saturating, autopush at PUSH_THRESH;
  OUT to every destination, PC, ISR (its count set) and EXEC (its own delay not taken) among them,
  autopull at PULL_THRESH; PUSH and PULL with IfFull, IfEmpty and Block (a non-blocking PULL of an empty
  FIFO takes X); MOV with every source (STATUS by TX level, RX level, or an IRQ flag of this block or
  either neighbour), invert and bit-reverse, to PINS, PINDIRS, EXEC, PC, ISR and OSR (resetting their
  counts); MOV to and from the RX FIFO's entries by Y or index; IRQ set, wait and clear with REL, PREV
  and NEXT; SET. Delay, side-set (SIDE_EN, SIDE_PINDIR), wrap (a jump taken at WRAP_TOP wins).
- **Pins**: a 32-bit level and direction per block in its own numbering, GPIOBASE placing it on GPIO
  0–31 or 16–47, and GPIO 0–47's levels read through a two-cycle synchroniser per pin that
  INPUT_SYNC_BYPASS removes. Side-set is made as the instruction issues, even when it stalls, and wins
  over its own state machine's OUT, SET or MOV; the higher-numbered state machine wins a pin on a cycle;
  OUT_STICKY asserts the last write again every cycle; INLINE_OUT_EN gates OUT and MOV to the pins by a
  data bit and lets a sticky write go. Tests drive the vector: FUNCSEL PIO0–2 is still refused, naming
  item 14.
- **FIFOs and flags**: 4 and 4, or 8 joined either way with the other reading full and empty; FJOIN_RX_PUT
  and _GET give the RX storage to MOV and RXFn_PUTGETm (the processor's only while exactly one is set);
  FSTAT, FLEVEL, FDEBUG's four sticky flags, INTR (RXNEMPTY, TXNFULL, all eight flags), IRQ0/1
  INTE/INTF/INTS and `irq_line` for item 14. IRQ flags change at the end of the cycle, so another state
  machine sees one on its next. SM_RESTART, SMx_INSTR (EXEC_STALLED while it stalls) and CTRL's
  NEXTPREV operations; blocks ACCESSCTRL makes one Non-secure and the other not are disconnected from
  each other's PREV and NEXT, as the datasheet says.
- **Refused by name**: every reserved encoding (IN source 4, MOV source 4 and operation 3, SET
  destination 3, IRQ bit 7, reserved PUSH/PULL/MOV-RX bits, WAIT JMPPIN beyond + 3), STATUS_N beyond the
  FIFO's depth or naming no flag, MOV to or from the RX entries without FJOIN_RX_PUT or _GET, and
  SIDE_EN with no side-set bits, each stopping the state machine with the run (`take_line_fault`);
  CLKDIV INT 0 with a fraction, STATUS_SEL 3, SIDESET_COUNT or SET_COUNT above 5, OUT_COUNT above 32, a
  read of TXFn, and RXFn_PUTGETm outside its mode, at the access. A FIFO or SMx_INSTR through an atomic
  alias is refused as an alias.
- **Assumptions**, since the headers do not say: SM_RESTART leaves the OSR empty (count 32); an OUT finding
  the OSR empty under autopull stalls that cycle while the refill happens at its end; under autopull a
  PULL is a no-op only while the OSR is full; a non-blocking PUSH to a full FIFO still clears the ISR;
  MOV OSR, RXFIFO[] leaves the OSR full; a system-written instruction runs on its state machine's next
  divider cycle, after any delay in progress, enabled or not; a flag set and cleared on one cycle ends
  set; changing any join bit flushes both FIFOs; a level driven in cycle t is read in cycle t + 2.

The oracles: `examples/pico2/pio/` holds pico-examples' `hello.pio`, `ws2812.pio`, `uart_tx.pio` and
`uart_rx.pio` (commit 0d62f75, unchanged but for a header comment) and benchtop's `rp2350_only.pio`, which
the SDK's CMake assembles with pioasm 2.3.0 built from `pico-sdk/tools/pioasm` into the committed
`*.pio.h`. `examples/pico2/pio_programs.c` prints each program's words beside the four registers the
pico-examples init has the SDK's `sm_config_*` compute; `chip/pio_tests.sysl` boots it, checks the words
against the headers line by line, and runs each at register level as `pio_sm_init` would: `hello_pio`'s
words reaching pin 25 two cycles after the TX write; `ws2812`'s pixel compared with its expected level on
every clk_sys cycle (a 1 high 108 cycles, a 0 54, a bit 180), stepping a cycle and 13 at a time; `uart_tx`'s
"Hello, world! (from PIO!)" read back off the pin by a UART decoder; `uart_rx` fed a 115200-baud waveform
on GPIO 3 (four bytes, a framing error raising IRQ 4, then a byte after the line idles); and the
RP2350-only pair across PIO0, PIO1 and PIO2 cycle by cycle, at GPIOBASE 0 and 16. The SDK's own numbers
caught one fact: pico-examples' `ws2812` clocks at divisor 18.0, not 18.75 (833 kHz, not 800), since it
divides `clock_get_hz`'s integer before making the float, on silicon as here. `rp2350/pio_tests.sysl`
(18 tests) covers each instruction, the divider, the FIFOs, the flags and every refusal at register level.
Breaking the synchroniser's delay, autopull's stall, side-set's priority, IRQ REL, X--'s test or the
divider's rounding each turns its tests red.

**Cost**, host instructions retired, interleaved runs against the branch's base (dev at 92a2019),
`/usr/bin/time -l`, `benchtop run --board pico2`: `sram_loop.elf` 5.407–5.426 G (median 5.411) to
5.412–5.422 G (5.413), eight runs each; `xip_loop.elf` 5.631–5.640 G (5.632) to 5.635–5.640 G (5.636);
`bench --core cortex-m33 kernel33.bin`, which never touches the chip, 36.875–36.897 G to 36.868–36.883 G.
With no state machine running the chip pays one flag test a quantum (`take_line_fault`), about 0.7 M of
these runs; the rest of the 0.04–0.06% is the base's own spread between sessions (10 M) and code layout,
as `kernel33` moving both ways shows. Two first cuts cost 0.08%: a test in `advance` on every clk_sys
step, and a `take_line_fault` that had grown past inlining; the blocks now run only when something can
see them, and the rare path is out of line (`next_line_fault`).

### Item 22, done: the RP2040 chip layer

`chip.Rp2040` (`chip/rp2040.sysl`) runs on `ArmBoard[Rp2040]`, the `pico` board, two Cortex-M0+ cores.
The SDK's `hello_serial` and `blink` and pico-examples' `multicore/hello_multicore` (unchanged), built for
`pico` / `rp2040` as `no_flash` images (`examples/pico`), run end to end: every line at 125 MHz, the LED
every 250 ms, both cores' lines after core 1's launch through the ROM's FIFO wait.

**The board is generic over its chip.** `ArmBoard[C: ArmChip = Rp2350]`: the trait is what the shared
run loop asks of a chip (clock, time, the next event, bus reads and writes, interrupt lines, core 1's
reset and release, the console), and a `const rp2350()` answer under which the RP2350's own machinery
(its ROM's hand-over, flash, XIP_SRAM, OTP, POWMAN, ACCESSCTRL, coprocessors, USB) is compiled with
`if const` for that instance alone. Generic rather than a trait object, because the bus is called on
every device access and an instance is monomorphised: the Pico 2 runs the code it ran before (Cost,
below). sysl has no per-instance `impl`, so an RP2350-only method body sits under `if const C.rp2350()`
at half indent; the Pico's boot is a free function over `ArmBoard[Rp2040]`.

**Shared models, parameterised:** `Sio` takes its FIFO depth (8) and the interpolators' shift
(`sio_rp2040`); `IoBank0` a GPIO count, where IRQSUMMARY starts, and CTRL's overrides at the RP2040's bits
(`io_bank0_rp2040`, with the RP2040's function table); `Pads` a count and whether ISO exists
(`pads_bank0_rp2040`, reset 0x56); `Psm` a stage count (`psm_rp2040`, 17). Mapped at the chip: XOSC's
COUNT (0x1C), ROSC without RANDOM, the PLLs without interrupt registers, the SIO's GPIO registers onto the
shared model's offsets. The RP2040's own: `ClocksRp2040` (`peripheral/rp2350/clocks_rp2040.sysl`: its
generators, 24.8 and two-bit dividers, clk_peri undivided, FC0 from its table; the slot where the RP2350
has clk_peri's DIV reads 0 and drops writes, because the SDK's `clock_configure` writes it); SYSINFO
(B2's CHIP_ID); the RP2040 watchdog with its TICK as the timer's 1 µs tick; each core's divider at SIO
0x060–0x078; SIO_IRQ_PROC0/1 (15, 16) each into its own core; USBCTRL's SIE_CTRL alone, stored, for
`runtime_init_usb_power_down`; the dormant wake on a dormant ROSC; SRAM's non-striped aliases through the
bus.

**The boot ROM is a stand-in** (`chip/rom/rp2040-standin.S`, assembled by `build-rp2040-standin.sh`),
until item 23 maps Raspberry Pi's. Its first 4 KB are mapped: the header (magic, version 3, the three
table pointers) and core 1's wait, the datasheet's protocol (2.8.2): drain, send core 0 a 0, echo 0, 0, 1,
VTOR, SP, ENTRY, restarting on a word out of place, sleeping in WFE. The tables, `rom_table_lookup`, the
NMI and HardFault handlers and core 0's boot path all point into the unmapped 12 KB, so a lookup, an
exception in the ROM or core 0 starting there is refused by name at the fetch. The examples therefore use
the compiler's float, double, bit operations and memcpy/memset (`examples/pico/README.md`);
`hello_multicore` needs no ROM function and runs on the stand-in.

**Found on the way, fixed:**
- **The M0+ decoded WFE and SEV as NOPs** whatever the board said (`v6m/decode.sysl`; the Thumb-2
  decoder honoured `events`), so core 1 polled its FIFO at full speed. Now as on the M33 (tests in
  `v6m/exceptions.sysl`).
- **The SDK's hardware divide needs cycle timing.** `divmod_u32u32_core` reads the quotient after a
  sequence counted to eight cycles; at one cycle an instruction it read a stale quotient, and PLL_SYS came
  up at 4.095 GHz. The Pico's M0+s run on the instruction timing table (`v6m/cycles.sysl`).

**Refused by name:** SSI, XIP and its control and cache-as-SRAM, VREG_AND_CHIP_RESET, RTC, IO_QSPI and
PADS_QSPI and the SIO's GPIO_HI bank (item 24); every ROM function and table (item 23); the watchdog
firing, by counter or TRIGGER (its reset is the ROM's boot path, item 23); SYSCFG, BUSCTRL, TBMAN, SPI,
I2C, PWM, ADC, DMA, PIO and the rest of USBCTRL, each with its item; a UF2 (flash, item 24); a write to
the ROM; `--usb` on the Pico; PSM FRCE_OFF of any stage but PROC1; FC0 on GPIN.

`chip/pico_tests.sysl` (19 cases) runs the three programs and checks each difference at its registers;
breaking the FIFO depth, the M0+'s timing or the WFE decode turns them red.

**Cost**, host instructions retired, three interleaved runs each against the branch's base (dev at
f6e7b41): `bench kernel.bin` (M0+) 40.207–40.211 G to 40.209–40.213 G; `bench --core cortex-m33
kernel33.bin` 36.846–36.850 G to 36.847–36.847 G; `sram_loop.elf` 5.401–5.406 G to 5.402–5.405 G;
`xip_loop.elf` 5.624–5.625 G to 5.624–5.625 G. Unchanged.

### Item 24, done: the RP2040's SSI and XIP, VREG_AND_CHIP_RESET and the RTC

`examples/pico` adds pico-examples' `rtc/hello_rtc` and `rtc/rtc_alarm` (unchanged), `ssi_flash`
(benchtop's own) and `blink.uf2`; `chip/pico_xip_tests.sysl` (16 cases) runs them and every register
rule and refusal.

**The flash path.** The Pico's flash is the RP2350's flash model (`parts/qspi_flash`) as a W25Q16JV
(`w25q16jv`: 2 MB, JEDEC EFh 40h 15h), connected to the board's array as on the Pico 2. The SSI
(`peripheral/rp2040/ssi.sysl`) times its frames at SCKDV clk_sys cycles a clock and is evaluated
lazily, whenever it, IO_QSPI or XIP is touched, so nothing is scheduled and the run loop pays nothing.
The timing is what puts chip select where silicon puts it: low while the TX FIFO feeds a command, up
when it runs dry, so boot2's back-to-back `01h 00h 02h` is one frame and a late byte is a new
instruction. IO_QSPI's SS override forces it, as the SDK's `flash_do_cmd` does. An XIP read is honoured
exactly where the SSI's configuration would read the flash: the read is framed as the SSI frames it
(XIP_CMD as an instruction before a 24-bit address, or as the mode bits of a 32-bit one, then the wait
cycles) and tried on a copy of the flash over a pattern, which must answer the address's bytes and
end in the state it began in. So boot2_w25q080's quad continuous read and boot2_generic_03h's 03h
both work, and 0Bh without its dummy byte, or a 32-bit address before continuous mode, is refused with
the registers named. While XIP is honoured the board puts the flash on the cores' fast path
(`sync_pico_xip`); a program or erase re-decodes what it changed, as on the Pico 2. XIP_CTRL, its
stream and the cache as SRAM at 0x1500_0000 (on the fast path while the cache is disabled) are the
RP2040's own (`XipCtrl`). `ssi_flash` drives it all with no ROM function: the IDs, an erase and two
page programs in direct mode, the SDK's own `boot2_w25q080.S` linked in and called, the four aliases,
a function programmed into flash and called through XIP, reprogrammed and called again (so the
re-decode is seen), the stream, a function run from the cache's SRAM.

**The loader** takes a UF2 of the RP2040's family (0xe48bff56), entering a RAM image at its first
byte as the ROM does; an ELF or UF2 may write the flash. An image that starts in flash is refused by
name: it boots through the ROM's boot path (item 23).

**VREG_AND_CHIP_RESET** stores VSEL and BOD, reads ROK, and reports HAD_POR, the one reset benchtop
makes; turning the regulator off or into high impedance is refused. **The RTC** counts clk_rtc into a
calendar a second at a time (the RP2040's leap rule, FORCE_NOTLEAPYEAR), takes LOAD and SETUP, latches
RTC_1 on a read of RTC_0, and raises IRQ 25 when the time changes to one matching every enabled
field, until MATCH_ENA clears; the chip schedules the next second through `due` only while the alarm
is armed.

**Not run, by name:** the SSI's receive-only mode, TI and Microwire formats, loopback, DDR, DMA and
interrupt (XIP_IRQ), and either FIFO's overflow or underflow; OUTOVER invert on SS; IO_QSPI's STATUS
and the SIO's GPIO_HI bank (the QSPI pins as levels, so the BOOTSEL read); the cache's counters; a
write to an uncached alias under ERR_BADWRITE; the cache's SRAM while enabled or powered down; an RTC
LOAD of a date the calendar does not hold, and CLKDIV_M1 changed while it runs. **Fidelity limits, not
refused:** the cache never makes a read stale; a store into the flash window's first 2 MB stops the
core at `Fault` where silicon deallocates a line; the RTC's clock-domain crossing takes no time; with
chip select forced high a byte reads FFh. pico-examples' `flash/program` is not run: its erase and
program go through the ROM's flash functions, which the stand-in refuses (item 23).

**Assumption, from the SDK rather than the datasheet:** the alarm is taken as the time changes, so
enabling it within a matching second raises nothing until the next match; the SDK's repeating alarm
re-enables from its handler within that second and would otherwise interrupt for the rest of it.

**Cost**, host instructions retired, three interleaved runs each against the branch's base (dev at
bc38ba4): `bench kernel.bin` (M0+) 40.208–40.215 G to 40.208–40.212 G; `bench --core cortex-m33
kernel33.bin` 36.846–36.853 G to 36.845–36.854 G; `sram_loop.elf` 5.408–5.413 G to 5.406–5.413 G;
`xip_loop.elf` 5.629–5.632 G to 5.629–5.635 G. Unchanged. `hello_multicore` on the Pico, a 0.3 G run
that is mostly boot, 0.315–0.319 G to 0.344–0.350 G: the 2 MB flash array the boot now lays out and
copies, once.

### Item 23, done: the RP2040's boot ROM, benchtop's own

**The image** is benchtop's own, written in sysl to work as the RP2040's does (`chip/rom/rp2040/`,
`chip/rom/README.md`): 16 KB at 0 on the cores' fast path, read-only, built by `image/build.sh`
(`sysl build-c --target thumbv6m-freestanding -Oz`, then Arm's `ld` with `rom.ld`), reproducibly:
SHA-256 `c85e249afdb8b577a03fab2e516fc588041a69a1db4631f0d541cb67d5c4d601` from sysl 0.8.5 and 0.8.6
alike, code and data to 0x3CA7. Raspberry Pi's image is not in the repository; neither it nor its
float code was read. **Clean-room sources:** the Pico SDK's `pico/bootrom.h`, `bootrom_constants.h`
and `pico/bootrom/sf_table.h` (the header, the codes, the tables' layout and the size byte before
each), `pico_float`'s and `pico_double`'s RP2040 callers (which entries, in which registers, with
which rounding: `float2int` toward −∞, `fsin` with the cosine in r1, the V3 double `sincos` in r0 to
r3, the trigonometric ranges ±128 and ±1024), `hardware_flash` (how the flash functions are used),
`boot_stage2` (boot2's entry with lr 0, `pad_checksum`'s CRC), `hardware_watchdog` (the reboot
vector), `hardware_regs`; and the datasheet-level facts they rest on.

**What runs as the ROM's code**, every line of it sysl but where the comment says why not:
- **the header and tables:** the vectors and the magic are a sysl struct in a section; the soft float
  and double tables are structs of typed function pointers (`tables.sysl`); the header's three 16-bit
  pointers and the function and data tables, whose entries are the low halves of link-time addresses
  that no language's constant expressions produce, are `SHORT` data in `rom.ld`;
- **core 1's launch wait:** it drains its FIFO and sends core 0 a 0, then echoes 0, 0, 1, VTOR, SP and
  the entry, a 0 keeping its place among the first three and any other word out of place answered 0,
  and starts the entry; it waits in WFE on its own stack at the top of USB's DPRAM;
- **core 0's boot path:** a SCRATCH4–7 vector the SDK's `watchdog_reboot` left (taken once); the QSPI
  pins and pads out of reset; BOOTSEL (SS with its output disabled, through GPIO_HI_IN); the flash out
  of XIP; boot2 read with the serial 03h command into the top of SRAM5, its CRC-32 checked over its
  first 252 bytes, and entered with lr 0, so that boot2 enters the image;
- `rom_table_lookup`; popcount, clz, ctz, reverse; memset, memset4, memcpy, memcpy44 (word-wise
  where aligned, any length); the flash functions through the SSI (`connect_internal_flash`,
  `flash_exit_xip` with the data pads pulled down then up, `flash_range_erase` with the 64 KB block
  command where a block fits, `flash_range_program`, `flash_flush_cache`, `flash_enter_cmd_xip`);
  `reset_usb_boot` up to its watchdog reboot;
- **the soft float and double tables**, every entry: add, subtract, multiply, divide, square root and
  every conversion IEEE-correct (round to nearest even), denormals flushed as the datasheet documents;
  sine, cosine, tangent, eˣ, ln and atan2 in 64-bit extended floats, with 192-bit argument reduction,
  rounded once.
- **Assembly**, `hw.sysl`, four uses, each a thing no language's statements express: WFE, SEV, setting
  MSP and branching (a program's start from its vector table, core 1's own stack), and branching with
  lr 0 (how boot2 knows it came from the ROM).

The examples are built with the SDK's defaults, so float, double, the bit operations and memcpy/memset
go through the ROM.

**Where the ROM's documented behaviour is not IEEE, or IEEE leaves it open** (`oracle/oracle.sysl`):
a denormal input reads as a zero of its sign and a denormal result is delivered as one (after
rounding at the denormal precision, so a carry into the smallest normal survives); conversions to
integers round toward −∞ and saturate, ∞ and NaN by their sign, a negative value to an unsigned
integer 0; an invalid operation answers the default NaN, a NaN operand itself quieted; `fcmp`
answers 1 for an unordered pair; sine, cosine and tangent take |x| < 2048 (wider than the ROM's
documented ±128 and ±1024) and answer NaN beyond.

**The watchdog's reset** is modelled on the Pico now: the PSM stages WDSEL selects go back to power-on
(as `Rp2350.watchdog_reset` does; RESETS.WDSEL where RESETS' own stage is not selected), the
watchdog's REASON, SCRATCH and tick survive, and so do SRAM, USB's DPRAM and the flash part. Each reset
core starts again in the ROM, whose boot path takes a SCRATCH4–7 vector or boots the flash.
`hello_watchdog` boots twice through it.

**Modelled in this item** (the first of them for the ROM, the rest kept from an earlier attempt that
ran Raspberry Pi's image, whose code needed them; programs reach them too):

| what | where | model |
|---|---|---|
| the ROM's doorbell, its last word | `chip/rp2040.sysl` (`rom_doorbell_refusal`) | a word stored there is the ROM refusing by name: USB boot (BOOTSEL held, no valid boot2, or `reset_usb_boot`), a left-out lookup code, the tables' flags comparison, an exception through the ROM's vectors |
| SCR | `v6m/t2system.sysl` | every profile's SCR: SLEEPONEXIT, SLEEPDEEP, SEVONPEND stored, SLEEPDEEPS with the Security Extension; an M0+ and a Main core without it used to stop at `Unmapped` |
| USB's DPRAM, core 1's stack in the ROM | `chip/rp2040.sysl` | 4 KB of memory, any width, ungated; the controller still refused |
| TBMAN PLATFORM | `chip/rp2040.sysl` | reads ASIC, ungated; the rest refused |
| SYSCFG PROC0/1_NMI_MASK | `chip/rp2040.sysl` | stored, 26 bits, ungated; the rest refused; nothing raises an NMI |
| SYSINFO read while RESETS holds it | `chip/rp2040.sysl` | ungated |
| GPIO_HI_IN (the BOOTSEL read) | `chip/rp2040.sysl` | SS is the flash's chip select, high unless driven low (the button is never pressed); the other five pins read 0, their levels not modelled; the rest of GPIO_HI refused |
| PADS_QSPI output disable on the data pads (`flash_exit_xip`) | `peripheral/rp2040/ssi.sysl` | the flash sees the pad's pull, not the SSI's bits: pulled down, mode bits of 0 end continuous read, and an instruction of 00h is ignored in SPI mode; pulled up, FFh; with no pull, refused by name |

**Found on the way, fixed:**
- **A core's overshoot of a quantum was lost.** An instruction ends where it ends, past the quantum's
  end, and the next quantum started the core at the nominal end, so the cycles between its last access
  and the boundary vanished from the chip's time. The SDK's hardware divide, which reads its result
  eight cycles after the write, read a stale quotient when a boundary fell between them: `printf` of
  `%08lx` printed a digit twice. The board now starts each core where it reached (`ArmBoard.reached`),
  for both chips (`chip/pico_tests.sysl`, every quantum length from 1 to 23 cycles). A run whose last
  access took the chip past its end no longer underflows computing the sleep. On the Pico 2,
  `uart_loopback.c`'s receive timeout now reads `after 802 us` where it read 804 (798.6 µs on the
  line; the interrupt is taken at a quantum's end, which now falls where the cores reached).

**Refused by name**, through the doorbell: USB boot (BOOTSEL held; the flash holding no valid boot2;
`reset_usb_boot`, named already at the watchdog's reset); the lookup codes the RP2040's ROM has and
this one leaves out ('D', 'T' `debug_trampoline`, 'D', 'E', 'W', 'V' `wait_for_vector`, 'F', 'S' and
'F', 'E'); the soft float and double tables' comparison that answers in the flags (offset 0x14),
which no function's result can; an exception taken through the ROM's own vectors.

**Tests and oracles.**
- `chip/rom40_float_tests.sysl` (9 tests) compiles the ROM's routines for the host and checks them
  against the host's IEEE arithmetic and libm (`sh.sysl.rp2040rom.oracle`): 30,000 to 60,000
  edge-biased cases per routine (zeros, denormals, the extreme normals, ∞, NaN, ties, neighbours a
  unit apart, cancellations), the documented edges each by name, and the double scientific functions'
  correctly rounded values at 25 arguments from `bc -l` at 80 digits.
- **The sweep**, `chip/rom/rp2040/examples/sweep`, built `-O2`, run once: every one of the 2^32
  inputs of `fsqrt` (150 s), of every float-to-integer and fixed-point conversion and `float2double`
  (65 s), and of every 32-bit integer to float and double (120 s), each with no mismatch; 16 million
  cases of the four float operations and of the four double operations, 4 million `dsqrt` and 4
  million double-to-integer conversions, all bit-exact; 6 million each of the float and the double
  scientific functions within one unit (float) and three (double) of the host's libm. macOS's libm is
  the looser side: its `tan` misses the correctly rounded value by up to three units where the ROM's
  is right (bc), while the ROM's is within about 0.502 of a unit: it rounds wrongly only when the
  exact value lies within about 2^−9 of a unit of a rounding boundary (its `tan(−1.0229…)`,
  `BFF05E00014D9CC7`, is …CF where …CE.7FE5 rounds to …CE).
- `chip/pico_tests.sysl`: every entry of both tables called on the Pico as the SDK calls it, bit for
  bit against the host build of the same routine (1,488 calls); the image's layout; the doorbell's
  refusals; core 1's launch; `rom_funcs`, pico-examples' `hello_world/serial`, `flash/program` and
  `watchdog/hello_watchdog` as flash images, and `hello_serial`, `blink` and `hello_multicore` on the
  SDK's defaults; boot2 with a broken CRC; the watchdog's reset; **the Pico SDK's own float tests**
  (`pico_float_test`, `pico_double_test`, `custom_float_funcs_test`, `custom_double_funcs_test`,
  `examples/pico/sdk_tests`), flash images on the ROM, each `PASSED`; and **the cross-check
  firmware** (`examples/pico/rom_crosscheck`), 71,248 cases, no mismatch, `CROSSCHECK 71248 0
  8c217b56`, the line a real Pico must print with its own ROM on the other side (its README). The
  SDK's `pico_divider_test` is not run: it ends in an endless loop by design, and its nesting test
  needs DMA; neither goes through the ROM.
- Breaking the ties-to-even rounding turns three of `rom40_float_tests` red; inverting the BOOTSEL
  level turns the flash-image boot red with USB boot named.

**sysl findings** (reported; the ROM written around none of them): (1) a `@section` val whose
initializer takes a function's address as an integer is silently zero in a `build-c` archive; (2)
`-Os`/`-Oz` do not mark functions `optsize`/`minsize`, so fixed-count loops unroll (at -Os the ROM is
18.9 KB; shared division and root routines bring -Oz to 15.5 KB); (3) `@export` beside `@section`
and `@align` on a binding is refused, so `rom.ld` aligns the soft float tables' size bytes itself.

**Cost**, host instructions retired, three interleaved runs each against the branch's base (dev at
1cf881d): `bench kernel.bin` (M0+) 40.207–40.213 G to 40.206–40.214 G; `bench --core cortex-m33
kernel33.bin` 36.847–36.853 G to 36.845–36.849 G; `sram_loop.elf` 5.407–5.413 G to 5.412–5.416 G;
`xip_loop.elf` 5.629–5.636 G to 5.633–5.634 G. Unchanged. `hello_multicore` on the Pico, 0.344–0.346 G
(dev's build, the compiler's float on the stand-in) to 0.348–0.350 G on this ROM with the SDK's
defaults; Raspberry Pi's image ran it in 0.349–0.355 G.
