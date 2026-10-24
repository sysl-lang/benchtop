# cortical — a Cortex-M board emulator written in sysl

**Status: a design note. No code exists.** The repo is `sysl-lang/cortical`.

cortical emulates Cortex-M boards well enough to run, unmodified, the firmware a developer would
flash onto them: each core on a host thread, a board's display in a Mac window, its UART on a
terminal, and skiff on every core. It is an interpreter now and is shaped so that a JIT can replace
the inner loop later. It is written in sysl, and part of its job is to stress the language. A gap
found while writing it is reported, not worked around (Rule 9). A board is a **description**: a sysl
package that names its cores, memory, devices, wiring and panel. Another board is another
description, not another emulator.

**The boards, in order** (decided 2026-10-23):

1. **Raspberry Pi Pico 2 (RP2350) first** (§9). The user owns one, so the real board is the oracle.
   Its two Cortex-M33s are ARMv8-M Mainline with DSP and FPv5-SP, a near-superset of the H7's M4, so
   the core work transfers. The org already has Pico 2 code (`pico2`, `pico-scratch`, `ogol-pico2`,
   `solder-pico2`, the SVD-generated `rp2350`, and skiff's milestone 5), which cortical can then run
   in CI.
2. **Then the Pico + the GeeekPi 3.5" breadboard kit (RP2040, 52Pi EP-0172)** (§10). The user owns it, and `picokit` is the
   org's program for it. It adds ARMv6-M, SPI, I²C, ADC and a 320×480 panel, at little cost once the
   RP2350 exists.
3. **Then the STM32H747I-DISCO** (§0, §3, §4): the user's project target, not yet owned. It adds
   the M7 (FPv5-DP), the DSI display and ST's peripherals.
4. **Later, cheaply:** the micro:bit (v1 is nRF51822, Cortex-M0; v2 is nRF52833, Cortex-M4F). The
   user owns both.
5. **Out of scope:** the RP2350's two Hazard3 RISC-V cores. If they come later, the bus, devices,
   virtual time and the panel are shared; only the core is new.

**One `Core`, many `CoreKind`s** (§2.1): an M33 (`V8MMain` with `security`), an M0+ (`V6M`), an M7
and an M4 (`V7EM`) are one interpreter with different descriptions, so every board above is a
description plus its devices.

Sources, and how far each was checked:

- **UM2411** (DISCO user manual). The ST PDF timed out when fetched. The facts below come from ST's own
  BSP, `STMicroelectronics/stm32h747i-disco-bsp` (`stm32h747i_discovery.h`, `_lcd.c/.h`, `_ts.h`,
  `_bus.h`, `_sdram.h`), which encodes the same wiring as code. UM2411 §5.2.3 (the power-supply
  solder bridges) is cited through an ST forum answer.
- **RM0399** (H745/747 reference manual). The PDF timed out when fetched as well. Chapters are cited
  **by title**, and their numbers still have to be read from RM0399 Rev 4's contents page. Table 95
  (bus-master IDs = HSEM core IDs) is cited through Embassy's HSEM docs. Register offsets marked (†)
  are from memory of RM0399 and `stm32h747xx.h`. Each is checked when its device is written:
  generated from ST's SVD (§3.0), never typed in from this note.
- **STM32CubeH7** (`STMicroelectronics/STM32CubeH7`, master): the example list in §5 was read
  directly from the repo tree with `gh api`.
- **skiff**: `~/dev/sysl-lang/skiff` at `origin/dev` 3cec4b3 (milestone 6 done on every QEMU core).
- **RP2350 datasheet** (Raspberry Pi, RP-008373-DS-3). The PDF exceeds the fetch limit, so it is
  cited **by chapter and section title**. The numbers are filled in when each device is written.
  Every RP2350 fact in §9 was checked against the pico-sdk in `~/dev/sysl-lang/pico-scratch/pico-sdk`
  (`src/rp2350/hardware_regs/include/hardware/regs/*.h`, `platform_defs.h`, and the `rp2_common`
  sources), and the file is named beside the fact.
- **The breadboard kit**: the user's board is the GeeekPi "Expansion Module 3.5inch" for the
  Raspberry Pi Pico (Amazon B0C98G37PW), GeeekPi's edition of 52Pi's EP-0172, which is the board
  `picokit` targets (confirmed by the user 2026-10-23). Sources: the 52Pi EP-0172 wiki page, 52Pi's demo repo
  `geeekpi/pico_breakboard_kit` (read with `gh api`), and `~/dev/sysl-lang/picokit`'s README.

---

## 0. The H7: two facts that change the brief

1. **The DISCO cannot run the M7 at 480 MHz as shipped.** The board is wired for direct SMPS supply
   (UM2411 §5.2.3), and with SMPS the highest voltage scale is VOS1, so the most is 400 MHz. VOS0 and
   480 MHz need the LDO, which means moving solder bridges. Every ST DISCO example configures
   **M7 400 MHz / M4 200 MHz** (the HSEM, LCD_DSI and UART readmes all say so). cortical runs the core
   at whatever RCC programs, so the target is "a fraction of 400/200" for ST's code. The board
   description records the supply wiring. A program that selects the LDO on an SMPS-wired board
   locks the MCU on real hardware (ST forum), and cortical reproduces that as a reported lock, not a
   silent success.
2. **The panel controller depends on the board revision, and ST's BSP decides it at run time.**
   MB1166 boards up to and including the A08 revision carry the FRD397B25009 glass with an
   **OTM8009A**. MB1166-**A09** carries the FRD400B25025 glass with an **NT35510** (ST forum answer).
   `BSP_LCD_InitEx` probes the NT35510 ID over a DSI read first and falls back to the OTM8009A
   (`stm32h747i_discovery_lcd.c`). cortical therefore models both controllers, and the board
   description chooses one (`panel = nt35510` by default, since that is what a board bought today
   carries). The DSI read path has to be real, because the BSP's choice runs through it.

---

## 1. Architecture

### 1.1 Threads

| thread | runs | owns |
|---|---|---|
| main (UI) | SDL3 event loop and presenting; macOS requires the window on the main thread | the window, the texture, input mapping |
| core 0 (CM7) | the interpreter for the M7 | the M7's `CpuState`, NVIC, SCB, SysTick, MPU, decode cache |
| core 1 (CM4) | the interpreter for the M4 | the same for the M4 |
| io | blocking host I/O: the UART back-end (stdio, pty or TCP) and the files that tests write | host descriptors |

- **Devices run on no thread of their own.** A device is a passive object: an MMIO access runs its
  handler on the thread of the core that made it. A timed event (a timer overflow, the end of a UART
  character, LTDC vsync) runs on whichever core thread first finds it due (§1.3).
- **Each device has one lock**, held for an access or an event. Both cores really do reach the same
  devices on the H747 (RCC, GPIO, EXTI, HSEM), so the lock is necessary. It is uncontended in the
  common case, where each core owns its own peripherals.
- The io thread and the UI thread talk to devices only through queues stamped with virtual time:
  bytes received, key and touch events, and finished frames going the other way.
- **Deterministic mode** (`--deterministic`) runs both cores on **one** host thread, round-robin with
  fixed quanta, so a run is reproducible to the instruction. **Tests use it.** QEMU's MTTCG made
  skiff's AN521 boots flaky (skiff `CLAUDE.md`, milestone 2: a tick delivered up to 5 ticks late), and
  cortical should not repeat that. `--seed n` jitters the quanta, which explores interleavings
  reproducibly: a skiff race found at seed 1234 recurs at seed 1234.

### 1.2 The bus and memory map

The bus is a **per-core view**: a table from address to region, built from the description at
start-up, plus a per-core fast table for the interpreter (§2.4). A region is one of:

| kind | backing | behaviour |
|---|---|---|
| `Ram` | host memory | plain loads and stores |
| `Rom` | host memory | read-only to the core. Writes go to the FLASH interface's program and erase logic (later) or raise a BusFault |
| `Alias` | another region at an offset | the same bytes at a second address |
| `Device` | a device and its window | an MMIO call `read(offset, size)` / `write(offset, size, value)` |
| `Gated` | a region plus a predicate | present only while the predicate holds (FMC SDRAM before FMC is enabled; a clock-gated peripheral) |
| `Fault` | nothing | BusFault (precise for a load, imprecise for a store, per the core) |

The H747 map (RCC/SYSCFG facts per RM0399 "Memory and bus architecture"; addresses marked † are to be
checked against the SVD):

| range | what | M7 | M4 |
|---|---|---|---|
| 0x0000_0000 64 KB | ITCM | yes, private | no (BusFault) |
| 0x0800_0000 1 MB | flash bank 1 (M7's boot address by default, option byte BOOT_CM7_ADD0) | yes | yes |
| 0x0810_0000 1 MB | flash bank 2 (M4's default boot, BOOT_CM4_ADD0) | yes | yes |
| 0x1000_0000 288 KB | D2 SRAM1–3 alias for the M4's code bus † | no | yes |
| 0x2000_0000 128 KB | DTCM | yes, private | no |
| 0x2400_0000 512 KB | AXI SRAM (D1) | yes | yes |
| 0x3000_0000 128+128+32 KB | SRAM1, SRAM2, SRAM3 (D2) | yes | yes |
| 0x3800_0000 64 KB | SRAM4 (D3) | yes | yes |
| 0x3880_0000 4 KB | backup SRAM | yes | yes |
| 0x4000_0000 … | APB1/APB2/AHB1/AHB2 (D2): USART1 0x4001_1000 †, TIM, DMA1/2, DMAMUX1, OTG | yes | yes |
| 0x5000_0000 … | APB3 (D1): DSI host 0x5000_0000 †, LTDC 0x5000_1000 † | yes | yes |
| 0x5200_0000 … | AHB3 (D1): MDMA, DMA2D 0x5200_1000 †, FLASH 0x5200_2000 †, FMC 0x5200_4000 †, QUADSPI | yes | yes |
| 0x5800_0000 … | APB4/AHB4 (D3): EXTI 0x5800_0000, SYSCFG 0x5800_0400, I2C4 0x5800_1C00, GPIOA–K 0x5802_0000+0x400·n, RCC 0x5802_4400, PWR 0x5802_4800, HSEM 0x5802_6400 † | yes | yes |
| 0x9000_0000 | QUADSPI memory-mapped (later) | yes | yes |
| 0xD000_0000 32 MB | FMC SDRAM bank 2, the IS42S32800J (`SDRAM_DEVICE_ADDR`, `_sdram.h`) | gated on FMC | gated on FMC |
| 0xE000_0000 | each core's own PPB: NVIC, SCB, SysTick, MPU, FPU, DWT, ITM | own | own |

- **The PPB is per core by construction**: each view maps 0xE000_0000 to that core's own system-control
  device. A core never reaches the other's NVIC through memory, as on silicon.
- **What the description declares per region**: the cores that see it, its address in each core's
  view (aliases are separate entries), and whether it is shareable. Shareable is information only
  for now, and becomes the cache model's input later (§1.5).
- **Interconnect timing is not modelled.** AXI, AHB and the D1/D2/D3 bridges add no wait states.
  Instruction cost is a per-class cycle table (§1.3), with flash wait states taken from FLASH_ACR's
  LATENCY field, so a program that forgets to set LATENCY runs slower, not wrongly. Real silicon
  would fail outright in that case, and cortical reports it as a warning on the console.

### 1.3 Virtual time

- **Each core counts its own cycles** (`u64`). An instruction adds its class cost: 1 for most
  data-processing instructions, the load/store and branch figures for its class, `SDIV`/`UDIV` 2–12 by
  operand width, `VSQRT`/`VDIV` from the TRM table, plus flash wait states when executing from flash
  with the cache off. The table is a description input, one per core type. The point is plausible
  relative timing, not a cycle-accurate M7, whose dual issue and branch prediction are deliberately
  not modelled.
- **Cycles become time through the core's clock**, which RCC computes from what the program set:
  HSE (25 MHz on the DISCO) → PLL1 → SYSCLK → D1CPRE gives the M7's clock, and HPRE gives the M4's.
  Every RCC write that changes a clock recomputes each core's period in **picoseconds**. The
  machine's virtual time is picoseconds as a `u64`, which lasts 213 days.
- **Peripheral time is derived from the same clocks**: SysTick counts its core's clock (or /8); TIM*
  count their APB timer clock; LTDC's pixel clock is PLL3R (the BSP sets M=5, N=132, R=24 from 25 MHz,
  so 27.5 MHz); a UART character takes 10 or 11 bit times at the programmed BRR.
- **Events**: one machine-wide queue (a binary heap keyed by picoseconds, under its own lock) holds
  device events. A core thread, at the end of each quantum, runs every event due at or before
  `floor = min(t_core0, t_core1)`, the time both cores have reached. An event never runs ahead of a
  core that could still change its inputs.
- **Keeping two clocks loosely in step**: a core may run ahead of the other by at most `W` of virtual
  time (default 20 µs, `--skew`). A core that reaches `t_other + W` waits on a condition variable until
  the other catches up. `W = 0` with one-instruction quanta is lockstep (slow, for tests that hunt
  ordering bugs). Deterministic mode makes `W` irrelevant.
- **WFI/WFE/Stop**: a sleeping core jumps its time forward to its next wake (its SysTick, a due event
  routed to it, the other core's SEV), bounded by `t_other + W`, so an idle M4 costs nothing. When
  both sleep, the machine jumps to the next event.
- **Pacing against the wall clock**: `--realtime` (the default with a window) sleeps when virtual time
  runs ahead of the wall, so a 2-second LED blink takes 2 seconds and the display runs at its virtual
  60 Hz. When the emulator is slower than real time, virtual time just runs slow. Nothing drops,
  because the firmware sees only virtual time. `--fast` (the default headless) never sleeps.

### 1.4 Inter-core state

- **Shared RAM** is the same host memory in both views, so there is nothing to synchronise beyond §1.5.
- **HSEM** (32 semaphores, RM0399 "Hardware semaphore"): each `R[n]` is a host `Atomic[u32]` holding
  `LOCK | COREID<<8 | PROCID`. A 2-step write-lock or a 1-step read-lock (`RLR[n]`) is a
  compare-and-swap against the unlocked value. Unlock requires a matching COREID/PROCID. The core IDs
  are the bus-master IDs (RM0399 Table 95): CM7 = 3, CM4 = 1. A free semaphore sets its bit in
  `C1ISR` and `C2ISR`, raising HSEM1 (to the M7) or HSEM2 (to the M4) where `CnIER` enables it. That is
  the ST examples' "core notification", and skiff's natural doorbell on this chip.
- **Events between cores**: on the H745/747 each core's TXEV is wired to the other's RXEV, so `SEV` on
  one core wakes a `WFE` on the other (the ST HSEM readme lists SEV as a D2 wake source). cortical sets
  the other core's event register and signals its condition variable.
- **Interrupts**: every IRQ line is wired to **both** NVICs, and each core enables its own. EXTI keeps
  a mask per core (`CPUIMRx` for the M7, `C2IMRx` for the M4 †). A device raises a line through a
  small `Irq` handle; the line sets an atomic pending bit in each NVIC that has it enabled and pokes
  that core's "look at interrupts" flag, which the interpreter checks at block boundaries.
- **CM4 boot gating**: option bytes BCM4/BCM7 decide whether each core starts at reset, and
  `RCC_GCR.BOOT_C2` releases a held M4. The ST examples boot both and park the M4 in Stop until the M7
  frees an HSEM. The option bytes are in the board description (defaults as shipped: both boot).
- **Per-core clock enables**: RCC has a common enable set and per-core sets (`RCC_C1_*ENR`,
  `RCC_C2_*ENR` †). A peripheral is clocked if either enables it. cortical models the OR, plus a
  per-core "allocated" view for the D2-in-Stop logic (a domain stays awake while any core has
  allocated something in it).

### 1.5 Memory-ordering assumptions

- **In parallel mode the host is Apple silicon (AArch64, weakly ordered).** Guest RAM accesses compile
  to host loads and stores of the same width, and aligned accesses up to 32 bits are single-copy
  atomic on both sides. Guest `DMB`/`DSB` are host `dmb ish`; `ISB` drains the decode-cache check
  (§2.4).
- **The M7 really is weakly ordered for Normal memory**, so plain host accesses are a fair model. The
  M4 in practice performs its memory accesses in order, so firmware that "works on the M4" may rely
  on it. M4 stores are therefore emitted as store-release and M4 loads as load-acquire (`stlr`/`ldar`,
  nearly free on Apple cores), and the M4 stays in order as on silicon.
- **Exclusives (`LDREX`/`STREX`) model each core's *local* monitor only.** On the H747 there is no
  global exclusive monitor between the M7 and the M4 for ordinary SRAM. That is why HSEM exists, and
  why skiff's ROADMAP names HSEM as the H7's lock. A cross-core spin lock built on exclusives
  therefore does **not** exclude on silicon, and it must not exclude in cortical. The exact silicon
  behaviour of an exclusive store to shareable memory on this interconnect (succeed without
  excluding, or always fail) still has to be read from RM0399 and the Cortex-M7 TRM. It is listed in
  §8 as a fact to pin down before the HSEM milestone, with an option, `--exclusives
  local|fail-shared`, so both readings can be run.
- **The RP2350 is the opposite case, so the exclusive model is a chip fact in the description.** The
  SDK's spin locks on the RP2350 are software locks built on `LDAEXB`/`STREXB` in SRAM (§9.3), so
  exclusives between its two M33s must exclude: the RP2350 chip description selects `--exclusives
  global`, a monitor shared by both cores over SRAM. The RP2040's M0+s have no exclusives at all.
- **Device (MMIO) accesses are strongly ordered and serialised** by the device lock.
- **Caches are no-ops (the brief).** That makes cortical *more* coherent than silicon: a missing clean
  or invalidate works in cortical and fails on the board. Since skiff's milestone 9 is exactly "shared
  state uncached on the M7, then the cache trait", a **later** option, `--dcache-model`, keeps a
  shadow D-cache for the M7 (16 KB, 4-way, 32-byte lines, write-back per the MPU attributes). It
  returns stale data where silicon would, which turns a coherence bug into a visible failure. The bus
  carries the MPU's cacheability bits now so the option has its input.

---

## 2. The CPU interpreter

### 2.1 What is executed

| | M7 (core 0) | M4 (core 1) |
|---|---|---|
| ISA | ARMv7E-M Thumb-2: all 16- and 32-bit encodings, IT blocks | same |
| DSP | SIMD add/sub (`SADD16`, `UQSUB8`, `SEL` and GE flags), saturating (`QADD`, `SSAT`, `USAT16`), the multiply family (`SMLAD`, `SMLALD`, `SMMLA`, `SMUAD`…), packing and extends (`PKHBT`, `SXTAB16`) | same |
| FPU | **FPv5-D16, single and double**: FPv4's set plus `VSEL`, `VMAXNM`/`VMINNM`, `VRINT*`, `VCVTA/N/P/M`, and every op at `.F64` | **FPv4-SP-D16, single only**: a `.F64` op is UNDEFINED, a NOCP/UNDEFINSTR UsageFault |
| MPU | PMSAv7, **16 regions** | PMSAv7, **8 regions** |
| caches | 16 KB I + 16 KB D, as no-ops; every `ICIALLU`/`DCIMVAC`/`DCCMVAC`/`DCCIMVAC`/`DCISW`… write to the SCB cache-maintenance registers is accepted, and CCR.IC/DC are stored | none |
| TCMs | ITCM 64 KB and DTCM 128 KB as RAM regions in the M7's view; `ITCMCR`/`DTCMCR` stored | none |
| CPUID | 0x411F_C27x (part 0xC27), which `HAL_GetCurrentCPUID` reads | 0x410F_C24x (part 0xC24) |

- **One core implementation, many instances.** A core type is a description:
  `CoreKind { arch: V6M | V7M | V7EM | V8MBase | V8MMain, dsp, fpu: None | Sp | Dp, mpu_regions,
  security, cpuid, nvic_prio_bits, irqs, cycle_table }`.
  - Each core on a board is an instance of the same `Core`, holding its own `CoreKind`, its own bus
    view, clock and reset vector.
  - The decoder consults the kind once per instruction it decodes, never on each execution: an M4
    decoding `VADD.F64` produces an "undefined" record. So the settings cost nothing on the hot
    path, and no feature test runs per executed instruction.
  - The same `Core` with other kinds covers every board in the order: an RP2350's M33s
    (`V8MMain`, FPv5-SP, `security: SecureOnly`, §2.7), an RP2040's M0+s (`V6M`, §2.8), and an H7's
    M7 or M4 (`V7EM`). `security` is `None | SecureOnly | Full`; only `SecureOnly` is built until a
    Non-secure program needs `Full`. The kind also names the core's coprocessors (§2.7), which are
    board devices. Only the ISA levels a
    board needs are implemented. A kind naming an unimplemented level is refused when the
    description is loaded.
- **The flags** are kept apart (N, Z, C, V, Q each a byte; GE a nibble) so that an instruction sets
  only what it needs. `xPSR` is assembled when read (MRS, exception stacking).
- **Floating point runs on the host FPU.** Arm's AArch64 and AArch32 VFP share IEEE behaviour, NaN
  propagation and default-NaN rules, so `f32`/`f64` host arithmetic is the guest's for the basic
  operations. FPSCR's rounding mode, FZ and DN map onto the host's FPCR bits, set on the core thread
  when FPSCR changes, and the cumulative flags come back from the host's FPSR. That is cheaper and
  more faithful than softfloat. Two things depend on probes listed in §8: that sysl reaches FPCR/FPSR
  as AArch64 system registers on a hosted build, and that `VFMA` maps to a host fused multiply-add
  (the `fma` `#if` symbol and `llvm.fma` say it does).

### 2.2 Exceptions, NVIC, SCB, SysTick, MPU (ARMv7-M ARM, chapter B1)

- **Entry**: stack 8 words, or 26 with the FP context (CONTROL.FPCA set, FPCCR.ASPEN). Lazy stacking
  (FPCCR.LSPEN, the default) is modelled properly: reserve the space, set LSPACT and FPCAR, and write
  the FP registers only when the handler first touches the FPU. skiff's PendSV reads EXC_RETURN bit
  4, and real handlers' stack depth depends on it. EXC_RETURN is formed, then come the
  mode/stack/CONTROL changes and the vector fetch through VTOR, which ignores the MPU (skiff's null
  guard relies on that, skiff DESIGN "What is guarded").
- **Return** on a load of EXC_RETURN into PC by `BX`, `POP`, `LDM` or `LDR`. Tail-chaining is modelled,
  because it changes what a handler observes in PSR and stack. Late arrival is not modelled, because
  only timing shows it.
- **Priorities**: 4 implemented bits (`__NVIC_PRIO_BITS = 4` on the H7), AIRCR.PRIGROUP, BASEPRI,
  BASEPRI_MAX, PRIMASK, FAULTMASK. Escalation to HardFault follows B1.5.
- **Faults**: MemManage, BusFault and UsageFault set CFSR/MMFAR/BFAR/HFSR exactly. skiff's stop-the-task
  path depends on MSTKERR/MUNSTKERR versus DACCVIOL and on MMARVALID. Precise versus imprecise
  BusFault is decided per access kind.
- **NVIC**: ISER/ICER/ISPR/ICPR/IABR/IPR/STIR for 150 lines.
- **SCB**: CPUID, ICSR (PENDSV/PENDST set and clear, VECTPENDING), VTOR, AIRCR (with SYSRESETREQ resetting
  the machine, or one core's domain per RM0399), SCR (SLEEPDEEP, SEVONPEND), CCR, SHPR1–3, SHCSR, the
  fault registers, CPACR, and FPCCR/FPCAR/FPDSCR.
- **SysTick**: CSR/RVR/CVR/CALIB. Counting is computed lazily from virtual time, not decremented per
  instruction.
- **MPU (PMSAv7)**: TYPE, CTRL (ENABLE, HFNMIENA, PRIVDEFENA), RNR, RBAR, RASR (SIZE, SRD subregions,
  AP, XN, TEX/C/B/S), and the alias registers. The highest-numbered matching region wins, and the
  default map applies under PRIVDEFENA. **skiff rewrites the MPU on every context switch**, with
  regions as small as 64 bytes (its stack guard), so updating the fast table on an MPU write is on the
  critical path (§2.4).
- **DWT/ITM**: `DWT_CYCCNT` is provided because HAL and benchmarks read it. It is the core's virtual
  cycle count. ITM stimulus port 0 is a second console (SWO), which is cheap and useful. QEMU has
  no DWT (skiff-old's notes), so cortical improves on that.
- **Semihosting** (`BKPT 0xAB`, SYS_WRITE0/WRITEC/WRITE/EXIT_EXTENDED) is on by an option. It is how
  skiff's boot tests halt, so the same scripts can drive cortical and QEMU.

### 2.3 The dispatch loop

```
loop
    if self.attention.load(Relaxed) then self.take_interrupts_and_events()
    val block = self.cache.block_at(self.pc)        // decoded on a miss
    self.cycles += block.run(self)                  // returns at the block's exit, or early on a fault
    if self.cycles >= self.quantum_end then self.end_quantum()
```

- A **decoded instruction** is a small record: `op: Op` (a simple enum of roughly 450 cases, one per
  encoding-and-variant that executes differently), the register numbers, `imm: u32`, the shift kind and
  amount, `len: u8` (2 or 4), `cycles: u8`, and the flags-set bit. All operand extraction, the
  immediate expansions (`ThumbExpandImm`) and the IT-condition folding happen **once**, in the
  decoder.
- **Execution is a tail-calling handler chain.**
  - Each decoded op carries its handler's address. Each handler ends with `become next.handler(…)`,
    sysl's guaranteed tail call (LLVM `musttail`), the way `sysl-lang/forth`'s direct-threaded inner
    interpreter does (`forth/README.md` § "Direct threading through guaranteed tail calls").
  - The machine, the record pointer and the retired count ride in parameters across the chain.
  - A `match` over `Op` is the reference implementation the chain is checked against.
  - **Measured** (`bench/`, ARMv6-M, M5 Pro, sysl 0.5.4): the chain runs at ~1365 MIPS and the
    `match` loop at ~1195 MIPS over the same records and semantics, so the chain is the executor.
    The records are 16 bytes; register numbers are `u4`, so indexing the sixteen registers needs no
    bounds check (with `u8` fields the chain ran at ~1180 MIPS).
  - **One record per halfword of guest address** is the spike's decode cache: the next instruction
    is the next record and a direct branch target is a signed record count from the branch's own, so
    taking a branch is one add. Whether blocks (below) buy anything over it is measured once
    interrupts and the MPU exist.
  - **Measured by cycles, not wall time**: `time -l`'s per-process cycle and instruction counts are
    unmoved by other load on the machine, where MIPS swung by 3× between runs. Three changes took the
    chain from 3.83 to 3.38 cycles (16.0 to 13.7 host instructions) per guest instruction, −12%:
    a `CMP` and the `B<cond>` after it run as one handler, with the condition a value parameter
    (`h_cmp_imm_bcond[const K: u32]`), still decoding the branch into its own record so a jump
    straight to it finds it; V is kept as the operands and result of the last addition and worked out
    only when a signed condition reads it; and the memory fast path tests the page entry, not the
    byte's address. A NOP costs ~1.6 cycles and a simple ALU instruction ~3.2, so what remains is
    dispatch and handler length about equally. Guest registers living in memory cost nothing
    measurable: a chain of dependent `ADDS` runs as fast as an independent one.
  - **The profile says where the next factor is not.** 44% of the workload is a nine-instruction
    straight-line CRC loop with no pair as frequent as `CMP`/`B<cond>`, so further fusion reaches
    little of it; fewer dispatches there means compiling a block, which is the JIT.
- **A block** is a straight run of decoded instructions ending at a branch, an exception return, a
  write to PC, an instruction that changes interrupt masking (`CPSIE`, `MSR BASEPRI`), an `SVC`, a
  `WFI`/`WFE`, a barrier, or 64 instructions. Interrupts are taken only between blocks, which is legal
  because an interrupt's arrival time is not architecturally fixed. A masked-to-unmasked transition
  ends the block, so a pending interrupt is taken on the next instruction, as the architecture
  requires.
- **IT blocks** are decoded together. Each of up to four instructions carries its condition, and the
  ITSTATE is materialised only if an exception lands inside the block.

### 2.4 Decode caching and the memory fast path

- **The decode cache is per core and keyed by guest address**, in 4 KB code pages, each holding an
  array of 2048 slots (one per halfword) of `*Block` entries filled lazily. A page is decoded from the
  bytes the core's view returns for that address. ITCM, flash, AXI SRAM and SDRAM can all hold code.
- **Invalidation**: every RAM page has an `Atomic[u32]` *generation* in a shared table. A store to a
  page whose "has code" bit is set bumps the generation, and a block records its page's generation at
  decode. A core checks the generation when entering a block, which is one load and compare, so
  code written by the other core, by DMA, or by a debugger load is never executed stale. Flash
  changes only by program/erase, which bumps the generation too. ITCM and DTCM pages belong to one
  core and need no atomics.
- **The memory fast table**: per core, `[1 << 20]` entries of one word each (8 MB), one per 4 KB of
  the 4 GB space. An entry is a host pointer with its low bits as flags: readable, writable, executable,
  "has code", and "slow" (MMIO, a gated region, or a page split by an MPU region). A load is
  `entry = fast[addr >> 12]`, a flag test, and a host load at `entry & ~FLAGS + (addr & 0xFFF)`. Every
  other case goes down the slow path through the region table.
- **The MPU is folded into the fast table**: there is one table per privilege, and two pointers
  select the current one. An MPU write recomputes only the pages its old and new regions cover, so
  skiff's per-switch reprogramming of a few regions costs a few hundred entry writes. Pages split by a
  sub-4 KB region are marked slow and checked per access against the region list. skiff's 64-byte
  stack guard makes the page holding each stack's base a slow page, which is acceptable.

### 2.5 The interface a JIT plugs into

The interpreter and a future JIT meet at the **block**:

```
trait Exec
    // Run from block `b`'s first instruction; answer the cycles spent. On return, `cpu.pc` is the
    // next instruction, or the faulting one with `cpu.pending_fault` set.
    run(self, cpu: *CpuState, b: *Block) -> u32
```

- `CpuState` is a struct with a **fixed C layout** (registers, flags bytes, FP registers, PSR parts,
  the fast-table pointers, the attention flag), so generated code addresses it by constant offsets.
- A `Block` carries its guest range, its page generation, a per-instruction PC side table (for precise
  faults inside a block), and an `exec` slot. The interpreter's `exec` is "walk the records". A JIT
  later fills a native entry, a raw `*extern(*CpuState, *Block) -> u32` (sysl's foreign
  function-pointer type, `types.md` "Function types"), and the dispatch loop does not change.
- **Helpers the JIT calls back into** are `@export`ed sysl functions with C signatures: slow-path load
  and store, an MMIO access, exception entry, and the MPU check. The JIT emits only the fast path
  inline.
- **The host facts a JIT will need** on macOS/arm64 are `mmap(MAP_JIT)`,
  `pthread_jit_write_protect_np` and `sys_icache_invalidate`. These are libc externs, and none is a
  language question today.

### 2.6 Instructions per second — the realistic target

sysl compiles through LLVM, so a careful interpreter in sysl should run within about 10–20% of the
same interpreter written in C. The comparison points are predecoded, switch-dispatched ARM
interpreters on a current Apple P-core, which reach 100–300 million guest instructions per second
before any JIT. Against that:

**The first measurement beat this table's estimates by about ten.** The `become` chain in `bench/`
runs an ARMv6-M workload at ~1365 MIPS on one M5 Pro P-core, with the page-table memory path
and lazy N/Z flags but no interrupts, MPU or IT blocks yet; compare-and-branch fusion and lazy V
have since cut its cycles per guest instruction by a further 12%. The estimates below are kept as the
floor to defend; the benchmark after milestone 1 replaces them with Thumb-2 figures.

| stage | per core | M7 (as a share of 400 MHz × ~1.3 IPC) | M4 (200 MHz × ~0.9 IPC) |
|---|---|---|---|
| first correct version (milestone 1 on the M33; the H7 at milestone 5) | 40–60 MIPS | ~10% | ~30% |
| **target**: predecoded blocks, fast table, lazy flags | **120–180 MIPS** | **~25–35%** | **~70–100%** |
| stretch: superinstructions for hot pairs, flag liveness | 200–250 MIPS | ~40–50% | real time |
| a JIT (later) | 800–2000 MIPS | real time | real time |

The two cores are independent host threads on two P-cores, so the figures do not halve. The M4 at
real speed is within reach of an interpreter. The M7 is not, since dual issue makes it about 520 M
instructions/s at 400 MHz, so "a sizeable fraction" for the M7 means a quarter to a half. Measure,
don't guess: the last item of milestone 1 is a benchmark harness (CoreMark built for every core, plus
ST's `FPU_Fractal` frame time once the H7 runs) reporting MIPS per core, run under `caffeinate -dimsu` on P-cores.
The QoS probe is in §8.

The harness runs on the Pico 2 first (milestone 1, §7), where the target is easier. An M33 at 150 MHz
and ~1 IPC is ~150 MIPS, so the interpreter's **target** row is real time for each RP2350 core. An
M0+ at 133 MHz is under 133 MIPS, which the target row also covers.

### 2.7 ARMv8-M Mainline: what the RP2350's M33s add over v7E-M

The RP2350's cores are Cortex-M33 r1p0 (`M33_CPUID_RESET 0x411fd210`, `regs/m33.h:3291`) with DSP,
FPv5-SP, an 8-region MPU per security state (`M33_MPU_TYPE_DREGION_RESET 0x08`, `m33.h:4998`) and an
8-region SAU (`M33_SAU_TYPE_SREGION_RESET 0x08`, `m33.h:5490`). The reference is the ARMv8-M
Architecture Reference Manual (DDI0553), cited by chapter title. Over §2.1's v7E-M:

- **The security extension (TrustZone), Secure state only.** A Pico SDK image is an **ARM Secure**
  image (`picotool info` prints `image type: ARM Secure`, `pico-scratch/CLAUDE.md`), and the SDK's
  `crt0.S` never programs the SAU or leaves Secure state. So the minimum faithful model is:
  - the core resets into Secure state and stays there. Every banked register (MSP, PSP, MSPLIM,
    PSPLIM, CONTROL, PRIMASK, BASEPRI, FAULTMASK, VTOR, SysTick, the MPU, parts of the SCB) is its
    Secure copy;
  - the SAU (`SAU_CTRL`, `SAU_TYPE`, `SAU_RNR`, `SAU_RBAR`, `SAU_RLAR`) is stored, and with it
    disabled and `ALLNS` clear every address is Secure, which is what the SDK runs under;
  - `TT`/`TTT`/`TTA`/`TTAT` answer from the SAU and MPU as stored, since compilers emit them for
    `cmse_check_address_range`;
  - EXC_RETURN carries the v8-M bits (`S`, `DCRS`, `ES`, and `FType` in bit 4, which skiff reads) with
    Secure values. Code compares these values, so they must be exact;
  - SecureFault and `SFSR`/`SFAR` exist, so a stray `SG` or a Non-secure transition (`BXNS`,
    `BLXNS`, an EXC_RETURN or `FNC_RETURN` naming Non-secure) **stops the run with a named
    diagnostic, "Non-secure state is not modelled"**, rather than doing something plausible;
  - the Non-secure aliases of the PPB (`PPB_NONSEC_BASE 0xe0020000`) and SIO (`SIO_NONSEC_BASE
    0xd0020000`, `regs/addressmap.h:106–108`) are mapped and refused with the same diagnostic until
    something needs them;
  - ACCESSCTRL (`0x40060000`) is stored. Its reset state grants Secure code everything (datasheet,
    "Security" chapter, "Access control"), which is the state the SDK leaves it in.

  Running Non-secure code (a Secure loader with a Non-secure application, the SDK's `kitchen_sink`
  TrustZone examples) is a costed later item: banked state for both worlds, the integrity signature
  on exception entry, secure gateways and the IDAU. The boot ROM itself is Secure code (§9.2).
- **The stack-limit registers.** `MSPLIM`/`PSPLIM` make a push, an `SP` write or exception stacking
  below the limit a UsageFault with `UFSR.STKOF`, with `SP` left at the limit, and
  `CCR.STKOFHFNMIGN` decides what happens at HardFault/NMI priority. `crt0.S:424–428` writes
  `msplim` itself, and skiff's protection milestone uses `PSPLIM` as a stack limit on the AN521's
  M33 (skiff `c19e830`), so it is exact from the first M33 milestone on.
- **The new instructions over v7-M** (DDI0553, the instruction chapters): `LDA`/`LDAB`/`LDAH`,
  `STL`/`STLB`/`STLH`, `LDAEX`/`LDAEXB`/`LDAEXH`, `STLEX`/`STLEXB`/`STLEXH` (the SDK's software spin
  locks on the RP2350 are built on the byte forms, §9.3), `TT*`, `SG`, `BXNS`, `BLXNS`, and `MRS`/`MSR`
  of `MSPLIM`, `PSPLIM` and the `_NS` registers. `CBZ`/`CBNZ`, `MOVW`/`MOVT`, `SDIV`/`UDIV` and the
  DSP set are already v7E-M.
- **FPv5-SP** is FPv4-SP plus the FPv5 additions (`VSEL`, `VMAXNM`, `VRINT*`, `VCVTA/N/P/M`) at
  single precision: the M7's FPv5-DP decoder with `fpu: Sp` refusing `.F64`, as the M4 refuses it.
- **PMSAv8 instead of PMSAv7.** Regions are base and limit (`MPU_RBAR`/`MPU_RLAR`, 32-byte
  granularity) rather than a power-of-two size with subregions; attributes come from `MPU_MAIR0/1`;
  `AP` is two bits plus `XN`; an address matching **two** enabled regions is a MemManage fault rather
  than "the highest region wins". The fast table (§2.4) is rebuilt from the same inputs, so it is a
  second rule set over one structure, not a second MPU. skiff already writes PMSAv8 maps for the
  AN521 (`c19e830`), so its scenarios are the first test.
- **The coprocessor port, which on the RP2350 the SDK uses by default.** The M33's `MCR`/`MRC`/
  `MCRR`/`MRRC`/`CDP` reach three RP2350 coprocessors (datasheet, "Processor subsystem",
  "Cortex-M33 coprocessors"), gated by `CPACR` and `NSACR`:
  - **CP0, the GPIO coprocessor**: `PICO_USE_GPIO_COPROCESSOR` defaults to 1
    (`hardware_gpio/include/hardware/gpio.h:18`), so `gpio_put` is a coprocessor write;
  - **CP4/CP5, the double-precision coprocessor (DCP)**: the default `pico_double` on the RP2350 is
    the DCP implementation (`pico_double/CMakeLists.txt:116–126`), so any `printf("%f")` reaches it;
  - **CP7, the redundancy coprocessor (RCP)**: `crt0.S:409–423` enables it and seeds the stack-canary
    salts, and the boot ROM's hardening uses its checks. A failed RCP check is a fault on silicon, and
    in cortical it is a stop with the check named.

  In cortical a coprocessor is a **board device on the core's coprocessor port**, as an MMIO device is
  on the bus, so the M33 stays generic and the RP2350 description supplies the three.
- **Exceptions**: v8-M adds `AIRCR.BFHFNMINS`/`PRIS` and the Secure/Non-secure priority boosting,
  which are inert in Secure-only mode. The RP2350 has 52 IRQ lines (`NUM_IRQS`,
  `platform_defs.h:25`).

### 2.8 ARMv6-M: the RP2040's M0+s (and the micro:bit v1's M0)

A `V6M` kind is §2.1's decoder with every encoding outside ARMv6-M refused as UNDEFINED, which here
means a HardFault (ARMv6-M ARM, DDI0419, "The ARMv6-M instruction set"):

- **The instruction set is a subset**: the 16-bit Thumb encodings plus `BL`, `MRS`, `MSR`, `DMB`,
  `DSB`, `ISB`. There is no IT block, no `CBZ`, no exclusives, no hardware divide, no DSP and no FPU.
  The RP2040 adds its single-cycle multiplier, which only changes the cycle table.
- **The exception model is smaller**: HardFault only (no MemManage, BusFault or UsageFault, so every
  fault escalates), no `BASEPRI`/`FAULTMASK`, 2 priority bits, 32 IRQ lines, and the stacking rules
  of §2.2 without the FP context. `VTOR` exists on the M0+.
- **The MPU is the PMSAv6 subset of PMSAv7**: 8 regions with power-of-two sizes and subregions, no
  `TEX` remap, and the alias registers absent.

**Cost: 1.5–2 agent-days**, most of it the fault model and the tests: which v7-M encodings fault, and
how. QEMU's `microbit` machine (nRF51, M0) is the differential oracle for v6-M, as §5.1's QEMU harness
is for v7E-M and v8-M.

---

## 3. Peripherals: the STM32H747I-DISCO

§3.0 and §3.2 hold for every board. §3.1 is the H7's device list; the Pico 2's is §9 and the
breadboard kit's §10.

### 3.0 How a device is described

- **Registers are `@packed` bitfield structs** (sysl's `iN`-in-exactly-N-bits rule, `::Bits(n)` to and
  from the word; reference `attributes.md` "Bitfields"). One struct per register, one struct per
  register block, with offsets checked by `@assert(offsetof(...))`.
- **They are generated, not typed**: the org's `svd` tool turns ST's `STM32H747_CM7.svd` into sysl.
  Today it emits `const`s only, and it refuses `<dim>` arrays and clusters by name (svd `README`).
  cortical needs a second output mode, bitfield structs, plus `<dim>` support (GPIO, DMA streams and
  LTDC layers use them). That is a piece of work in `sysl-lang/svd`, done before the devices.
- **A device module** implements

  ```
  trait Device
      read(self, offset: u32, size: u8) -> u32
      write(self, offset: u32, size: u8, value: u32)
      reset(self)
  ```

  and keeps its register block as the generated struct. Behaviour is written against named fields:
  `if cr1.ue && cr1.te`. The offset-to-field dispatch is a `match` the generator also writes, unless the
  reflection probe in §8 shows `T::Fields` can derive it.
- **A board description** is a sysl module that builds a constant `Board`: cores, regions, device
  instances with base addresses and IRQ numbers per core, wires (a GPIO pin to an LED, a joystick key
  to a pin, an I2C bus to a chip, the DSI link to a panel), clocks (HSE 25 MHz, LSE 32.768 kHz), option
  bytes, supply wiring, and the panel layout for the window. A new board is a new module.

### 3.1 The devices

| device | registers modelled | behaviour | ignored |
|---|---|---|---|
| **RCC** | CR, HSICFGR, CSICFGR, CFGR, D1CFGR, D2CFGR, D3CFGR, PLLCKSELR, PLLCFGR, PLL1–3DIVR/FRACR, D1/D2/D3CCIPR, BDCR, CSR, the reset and enable sets (common + C1 + C2), GCR, RSR | every `…ON` bit makes its `…RDY` true at once (or after a modelled start-up of a few µs, as an option); SW→SWS follows; computes every clock in §1.3; enable bits gate devices (an access to a gated peripheral reads 0 and writes are dropped, as on silicon); reset bits reset the device | clock security, the HSE bypass analogue, real lock times |
| **PWR** | CR1, CSR1, CR2, CR3 (SMPS/LDO/BYPASS, the write-once rule), CPUCR, D3CR (VOS, VOSRDY), WKUP* | ACTVOSRDY and VOSRDY follow at once; checks the supply choice against the board's wiring (§0); CPUCR's per-core Stop/Standby flags; D1/D2 domain Stop/Run per the core states | voltage levels, the PVD/AVD thresholds |
| **FLASH** (interface) | ACR (LATENCY, WRHIGHFREQ), KEYR1/2, OPTKEYR, CR1/2, SR1/2, CCR1/2, OPTCR/OPTSR, the BOOT_CM7/CM4_ADD option registers | LATENCY feeds the cycle table; the option registers read back the description's option bytes; program/erase later (a flashing tool can load ELFs directly meanwhile) | ECC, read-out protection, bank swap |
| **SYSCFG** | PMCR, EXTICR1–4, CCCSR, PWRCR (ODEN), UR0–UR17 (read-only option mirrors) | EXTI line source selection; the rest stored | I/O compensation |
| **GPIO A–K** | MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRR, LCKR, AFRL/H | pin state = output, or input from a wire (button, joystick, the touch INT); edges go to EXTI; ODR drives LEDs and the panel's reset/backlight (PG3, PJ12) | drive strength, analog mode |
| **EXTI** | RTSR1–3, FTSR1–3, SWIER1–3, D3PMR/D3PCR, CPUIMR/CPUEMR/CPUPR1–3, C2IMR/C2EMR/C2PR1–3 | edge detection, per-core masks and pending bits, raising EXTI0…15 IRQs and the direct lines (HSEM, USART wake) on each core | D3 autonomous mode beyond what Stop needs |
| **USART1** (the ST-LINK VCP: PA9 TX, PA10 RX, AF7, `stm32h747i_discovery.h` COM1) | CR1–3, BRR, GTPR, RTOR, RQR, ISR, ICR, RDR, TDR, PRESC | 8/7/9-bit with parity, 1/2 stop, the 16-deep FIFOs and thresholds (`UART_WakeUpFromStopUsingFIFO` needs them), TXE/TXFNF/TC/RXNE/RXFNE/ORE/IDLE with interrupts, timing per baud in virtual time (or instant, `--uart-instant`), wake from Stop; framing to the host back-end (§3.2) | IrDA, LIN, smartcard, hardware flow control, DMA requests until DMA |
| **HSEM** | R0–31, RLR0–31, C1IER/ICR/ISR/MISR, C2IER/ICR/ISR/MISR, CR (clear by core), KEYR | §1.4 | — |
| **LTDC** | SSCR, BPCR, AWCR, TWCR, GCR, SRCR, BCCR, IER, ISR, ICR, LIPCR, CPSR, CDSR; layers 1–2: CR, WHPCR, WVPCR, CKCR, PFCR, CACR, DCCR, BFCR, CFBAR, CFBLR, CFBLNR, CLUTWR | per frame at the virtual vsync: compose two layers over the background (window, all 8 pixel formats, constant alpha, the two blending factors, colour key, CLUT) from guest memory into the host frame; the line interrupt (LIPCR), register-reload interrupt, the shadow-register reload at vblank or immediately; FIFO-underrun never happens | dithering, pixel-exact timing within a line |
| **DSI host + wrapper** | CR, CCR, LVCIDR, LCOLCR, LPCR, LPMCR, PCR, GVCIDR, MCR, VMCR, VPCR, VCCR, VNPCR, VHSACR, VHBPCR, VLCR, VVSACR, VVBPCR, VVFPCR, VVACR, LCCR, CMCR, GHCR, GPDR, GPSR, TCCR0–5, CLCR, CLTCR, DLTCR, PCTLR, PCONFR, PUCR, PTTCR, PSR, ISR0/1, IER0/1; wrapper WCFGR, WCR, WIER, WISR, WIFCR, WPCR0, WRPCR | the HAL's start-up polls must succeed: the regulator-ready and PLL-lock flags (WISR RRS/PLLLS) set once enabled; **generic and DCS writes** (short and long packets through GHCR/GPDR) go to the panel model; **reads** fill the read FIFO from the panel (the BSP probes the NT35510 ID this way); **video mode** connects the LTDC output to the panel; **adapted command mode** (`WCR.LTDCEN` starts one frame, end-of-refresh flag and interrupt) for the CmdMode examples; TE from the panel by link or GPIO (PJ2) | the PHY's analogue timing registers (stored), ULPM beyond its flags, lane-level errors |
| **Panel: OTM8009A / NT35510** | the DCS commands both use: SLPOUT 0x11, SLPIN 0x10, DISPON 0x29, DISPOFF 0x28, MADCTL 0x36, COLMOD 0x3A, CASET 0x2A, PASET 0x2B, RAMWR 0x2C, RAMWRC 0x3C, TEON 0x35, TEOFF 0x34, STE 0x44, WRDISBV 0x51, WRCTRLD 0x53, WRCABC 0x55, RDID1–3 0xDA–0xDC; the vendor pages (OTM8009A's 0x00 shift + 0xFF enable; NT35510's 0xF0 page select) are accepted and stored | a frame memory for command mode (CASET/PASET windows, RAMWR streams, MADCTL orientation and RGB/BGR); video mode shows the stream through MADCTL; nothing is shown before SLPOUT + DISPON, with reset (PG3) held high and the backlight (PJ12) on; ID reads answer the IDs the BSP component headers check | gamma curves, CABC dimming beyond brightness, power-sequence timing |
| **DMA2D** | CR, ISR, IFCR, FGMAR, FGOR, BGMAR, BGOR, FGPFCCR, FGCOLR, BGPFCCR, BGCOLR, FGCMAR, BGCMAR, OPFCCR, OCOLR, OMAR, OOR, NLR, LWR, AMTCR, FG/BG CLUT | the four modes (M2M, M2M+PFC, M2M+blend, R2M), the formats, alpha modes, red/blue swap, alpha inversion (all six ST DMA2D examples exercise these), CLUT load; a transfer completes after a modelled duration and raises TC | line-offset modes beyond what the HAL sets, dead time |
| **FMC (SDRAM)** | BCR1 (FMCEN), SDCR1/2, SDTR1/2, SDCMR, SDRTR, SDSR | the BSP's init sequence (clock enable, PALL, auto-refresh, load mode) is accepted in order; SDSR.BUSY is always clear; once FMCEN is set, bank 2 maps 32 MB of RAM at 0xD000_0000 (IS42S32800J, `_sdram.h`) | refresh timing, NOR/PSRAM/NAND |
| **TIM2–7 + TIM1** | the general/basic/advanced register set as far as update, compare and DMA requests | counting from virtual time, the update interrupt, output compare for `TIM_DMA`; HAL time base on TIM6 if a program chooses it | PWM output to pins (later, the LEDs dimming), input capture, encoders |
| **DMA1/2 + DMAMUX1** (and MDMA as needed) | the stream registers, DMAMUX channel and request generators | memory-to-memory and peripheral requests from USART1 and TIM, completing in modelled time, half/complete interrupts; MDMA when an LCD example needs it (§5) | FIFO burst behaviour, priorities between streams |

Touch is optional. The cost is an **I2C4** controller (TIMINGR, CR1/2, ISR, ICR, TXDR, RXDR,
enough of the master mode for register reads and writes, PD12/PD13 AF4) plus an **FT6x06** model (I2C
address from `_ts.h`: 0x54 >> 1, or 0x70 >> 1 on the A02 board), registers TD_STATUS and P1/P2 XH/XL/YH/YL,
and the chip-ID and mode registers the BSP reads, with INT on **PK7** through EXTI. A mouse drag in
the display area becomes one touch, and two fingers on a trackpad become two (`TS_TOUCH_NBR = 2`).
**About 1–1.5 agent-days.**

### 3.2 The UART on the Mac

The VCP's line goes to one of three back-ends (`--uart`):

- **`stdio`** (default): cortical's own terminal in raw mode through `sysl.tty`. A REPL on the board is
  a REPL in the terminal cortical runs in. Ctrl-] leaves.
- **`pty`**: cortical opens a pseudo-terminal and prints its `/dev/ttys…` name, so `screen`, `picocom`
  or a host tool that expects a serial device attaches as it would to the real ST-LINK VCP. The
  pty's baud setting is ignored; the guest's BRR governs timing. **`posix_openpt`, `grantpt`,
  `unlockpt` and `ptsname` are missing from `cstd`** (`cstd/POSIX.md` lists them as missing), so this
  back-end waits on them (§8).
- **`tcp:PORT`**: a raw socket, for scripted tests (`sysl.net`).

Tests also use `--expect "line" --timeout 10s`, which exits 0 when every expected line has appeared
in order and 1 otherwise, as skiff's `boot-test.sh` reads QEMU's output today.

---

## 4. How it appears on screen

### 4.1 The panel description

The window is a drawing of the board, generated from the description's layout rather than taken from
a photograph:

- **The display area** shows exactly 800×480 guest pixels (`LCD_DEFAULT_WIDTH/HEIGHT`, landscape), at
  1×, 2× (Retina) or a fitted scale. When the panel is off, in reset, or the backlight pin is low, the
  area is black. A thin border shows "panel asleep" or "no DSI" states, for debugging.
- **The four LEDs**, LD1 green PI12, LD2 orange PI13, LD3 red PI14 and LD4 blue PI15
  (`stm32h747i_discovery.h`), are drawn lit or dark from ODR. Their polarity (active low on the
  DISCO, to be checked against UM2411's schematic) is in the description.
- **The joystick** has SEL PK2, DOWN PK3, LEFT PK4, RIGHT PK5 and UP PK6, clickable and on the
  arrow keys and Return. **WAKEUP** (PC13) is clickable and on `W`. **RESET** (NRST) resets the
  machine and is on `R`.
- A status line shows each core's clock as configured, its MIPS, how far virtual time lags the wall,
  and whether the core is running, sleeping (WFI) or in Stop.
- `--headless` runs the same machine without a window. `--screenshot t=2.5s:out.png` writes the
  displayed frame through the org's `png` package. Display tests compare these frames (§5).

### 4.2 The host UI library

**`sysl-lang/sdl3`** is used directly: a window, a renderer and one streaming texture of 800×480 ARGB,
which the LTDC model fills and the UI thread uploads with `SDL_UpdateTexture`, plus primitives for the
board drawing and the event queue for keys and mouse. It already exists and is released, owns its
handles as `&T` (sdl3 `README` "Handles own what they point at"), and is what `sdl3-demo` uses.
**syslui** (with `syslui-sdl`) is a widget toolkit. It is the right tool for a later *debugger
panel* (registers, memory, peripheral state, a disassembly), but too much for a picture of a board
with five buttons. Frames cross from the core thread to the UI thread through a triple buffer, so
neither ever waits for the other.

---

## 5. Fidelity and testing — the oracle is not cortical

### 5.1 Instruction semantics: differential against QEMU, with Arm's pseudocode deciding

- **The harness**: a generator writes small test programs (register and memory set-up, a random but
  valid run of N Thumb-2 instructions from one class, then a semihosting dump of R0–R12, SP, LR, the
  flags, GE, the FP registers and FPSCR, and a memory window). Each program runs on QEMU and on cortical,
  and the two dumps are compared. This is QEMU's own `risu` idea, done through semihosting because
  M-profile has no user mode.
  - **M7**: `qemu-system-arm -M mps2-an500 -cpu cortex-m7` (FPv5 with double precision; skiff already
    boots there).
  - **M4**: `-M mps2-an386` (cortex-m4, FPv4-SP).
  - Classes run separately: data processing with every shift, the flag setters, IT blocks, loads and
    stores in every addressing mode (unaligned included), the multiplies and divides, every DSP
    group, and FP arithmetic with rounding modes, FZ, DN, NaNs and denormals.
- **When QEMU and cortical disagree, neither wins by default**: the ARMv7-M Architecture Reference
  Manual's pseudocode for that encoding decides, and a QEMU bug found that way is recorded. QEMU's M
  profile has had DSP and FP-flag bugs before.
- **Exceptions** are compared the same way: a fault-provoking program dumps CFSR, the stacked frame and
  EXC_RETURN on both.
- **The final oracle is the board.** The user is buying one, and the same generated programs run on it
  through ST-LINK (`probe-rs` or OpenOCD with semihosting) settle anything QEMU and the manual leave
  open.
- These are `sysl test` tests in cortical. Each generated case that has ever failed is kept as a fixed
  regression case. Seeds are fixed per run and logged.

### 5.2 Peripherals and boards: ST's own examples, unmodified

ST's STM32CubeH7 on GitHub (`Projects/STM32H747I-DISCO`, read with `gh api`) carries **no UART-printf
example and no BSP demo for this board**. The LCD drivers live in `Drivers/BSP/STM32H747I-DISCO`, a
submodule pointing at `stm32h747i-disco-bsp`, and are used by the LCD_DSI and FPU examples. The
acceptance set is therefore:

| milestone | example (all under `Projects/STM32H747I-DISCO/`) | what passing looks like |
|---|---|---|
| M7 blinky & UART | `Examples/GPIO/GPIO_EXTI` | LED toggles when the button/joystick is clicked |
| | `Examples/RCC/RCC_ClockConfig` | clocks switch; LED pattern as the readme says |
| | `Examples/UART/UART_WakeUpFromStopUsingFIFO` (USART1, 9600 7O1) | Stop entered (LED1 off), woken by 4 then 16 bytes typed in the terminal |
| both cores | `Examples/HSEM/HSEM_CoreNotification`, `HSEM_CoreSync`, `HSEM_ResourceSharing` | LED1 toggles 10×200 ms on the M7, then LED3 on the M4 at the notification |
| | `Applications/FreeRTOS/FreeRTOS_HwSemaphoreCoreSync` | the readme's LED behaviour on both cores |
| | `Applications/OpenAMP/OpenAMP_PingPong` (stretch) | the ping-pong completes over shared memory |
| display | `Examples/LCD_DSI/LCD_DSI_VideoMode_SingleBuffer`, `…_DoubleBuffering` | the images in turn, 2 s each, as the readme says |
| | `Examples/LCD_DSI/LCD_DSI_CmdMode_SingleBuffer`, `…_DoubleBuffer`, `…_TearingEffect`, `…_TearingEffect_ExtPin`, `…_PartialRefresh` | the same through adapted command mode and TE |
| | `Examples/DMA2D/*` (six) | each blended/converted picture on the LCD |
| | `Applications/FPU/FPU_Fractal` | the fractal renders, on the M7 with the DP FPU and the M4 with SP, and the on-screen timing is the benchmark |

- **Where the binaries come from**: ST ships sources and IDE projects (EWARM, MDK-ARM, STM32CubeIDE),
  not ELF files. cortical's `tests/st/` holds a build script and a generic Makefile per example that
  compile **ST's files untouched** with `arm-none-eabi-gcc` (already on this machine). The files are
  the example's `Src/`, the CMSIS `startup_stm32h747xx.s`, the dual-core `system_stm32h7xx_dualcore_*.c`
  and the STM32CubeIDE linker scripts for CM7 and CM4. Each run produces two ELFs, flash bank 1 and
  flash bank 2. The CubeH7 tree is pinned by commit in the test manifest.
- **How passing is judged**: LED states and UART bytes are compared exactly, against a trace written
  from the example's readme. **Frames are compared against reference PNGs**, which are captured once
  from cortical, **checked by the user against the real board** (a photo or the user's eye), and then
  frozen. The user's own check is the oracle, not cortical. Until the board arrives, a frame passes on
  a set of hand-written properties (the background colour at known pixels, the image's bounding box),
  which is weaker, and the report says so.
- The `BSP` example (`Examples/BSP`) that older STM32CubeH7 releases shipped for this board is not in
  the current tree. If the user wants it, it can be taken from the release package that still has it,
  and that release has to be found first.

---

## 6. skiff on cortical

### 6.1 The Pico 2 first

skiff already names the RP2350 as its first real silicon: its ROADMAP milestone 5 is "Pico 2:
`chips/rp2350`, `boards/pico2` (one image, both M33s), the SIO FIFO doorbell, the lock (spinlocks or
exclusives, per erratum E2), UF2" (`skiff/ROADMAP.md:126`), and its Machines table lists the Pico 2
for "the SIO FIFO doorbell, the spinlocks (erratum E2)" (`ROADMAP.md:111`). At `origin/dev` 3cec4b3
neither `chips/rp2350` nor `boards/pico2` exists yet (the tree has `chips/an521`, `chips/mps2`), and
skiff's milestone 6 waits on milestone 5 for silicon. skiff's M33 work (PMSAv8 maps, `PSPLIM` stack
limits, both AN521 cores) is already done under QEMU.

So **skiff on the Pico 2 in cortical (milestone 4, §7) comes before skiff on the H7 (milestone 7)**,
and it is cheaper on both sides: the cores are the AN521's architecture, and what is new is the chip.
What cortical must provide faithfully for it:

- both M33s entering one image, core 1 released through the boot ROM's FIFO launch protocol (§9.6),
  the core number from `SIO_CPUID`;
- the SIO FIFO and its per-core IRQ as the doorbell, and the doorbell registers (§9.3);
- **both locks**: the hardware spinlocks (with erratum E2 modelled behind an option, so a lock that
  E2 would break fails in cortical) and exclusives that exclude across the cores (§1.5);
- PMSAv8 with 8 regions, `PSPLIM`, SysTick per core, PendSV/SVC, semihosting halt, as §6.2 lists for
  the H7.

**The request to the skiff session for milestone 5**, sent when cortical's milestone 2 (Pico 2 dual
core) runs: `chips/rp2350` and `boards/pico2` as their ROADMAP describes them, and a `boot-test.sh`
target that runs cortical headless with `--deterministic` beside the QEMU machines. Then skiff's
milestone 1–4 and 6 scenarios run on the Pico 2's shape in cortical before and alongside the board,
and the board confirms.

### 6.2 The H7

Where skiff stands (`origin/dev` 3cec4b3): milestones 0–4 done; **6 (protection) done on every QEMU
core** (AN500, AN386, both AN521 cores), waiting on milestone 5 (the Pico 2) for silicon and on
FINDINGS 9 for granting queues/channels/pools; 7 (the rest of the kernel) and 8 (two images) are next;
9 is "the H747I-DISCO (needs the board)". cortical lets milestone 9 start before the board arrives, and
makes its one-image half testable in a deterministic, scriptable way that the board cannot be.

What skiff uses, from `arch/armv7m`, `arch/pmsa`, `chips/an521` and `boards/qemu-an521`, and
therefore what cortical must provide faithfully:

- **Both cores entering one `_start`**, reading a core number, taking their own stack top, and core 0
  zeroing `.bss`/`.shared` before releasing core 1. On the H747 the core number comes from SCB
  CPUID's part number (0xC27 or 0xC24), and the release is HSEM-plus-Stop, or `RCC_GCR.BOOT_C2` with
  the M4 gated by option byte.
- **SysTick per core, PendSV and SVC, BASEPRI at 0x80/0xC0, PRIMASK, FP context save under EXC_RETURN
  bit 4** (§2.2).
- **The doorbell**: on the AN521 it is MHU0 (IRQ 6). On the H747 it is an HSEM channel per direction
  with `CnIER` set, so the HSEM1/HSEM2 interrupts carry the ring, or alternatively `SEV`/EXTI SWIER
  with a per-core EXTI line. cortical models all three.
- **The lock**: skiff's `spin.sysl` uses `Atomic.swap` (LDREX/STREX). **That does not exclude between
  the M7 and the M4** (§1.5), so the H747 chip must supply its lock from HSEM, as skiff's own ROADMAP
  says. cortical's default exclusive model makes an exclusives-based lock fail the m3a1-style hammer
  test (`the shared count is … not 200000`) on the H747 as it would on silicon. That is a test cortical
  is in a position to run.
- **PMSAv7 with 8 regions on the M4 and 16 on the M7**: `Isolation.capacity()` must answer per core,
  and a one-image build must size maps for 8. The 64-byte stack guard, the null guard over the first
  256 bytes (vector fetch ignores the MPU), unprivileged tasks, SVC kernel calls, and MemManage with
  MSTKERR/DACCVIOL deciding "stopped a task" all have to be exact.
- **Semihosting halt** for the boot tests, which cortical offers so `scripts/boot-test.sh` can drive
  cortical as it drives QEMU.

**The request to the skiff session for the H747** (their work, not cortical's). It is to be sent once
cortical's milestone 5 (H7 M7 + M4) runs the HSEM examples:

1. `chips/stm32h747`: start-up for both cores (CPUID part number as the core id; stack tops; CPACR;
   core 0 zeroes `.bss`/`.shared`; the M4's release through HSEM notification or `RCC_GCR.BOOT_C2`), the
   IRQ table to at least HSEM2 (≈126) and USART1, an HSEM doorbell, an **HSEM lock** behind the
   lock trait, `irq.sysl`, and `uart.sysl` on USART1.
2. Clocks and power for the one-image build: SMPS direct supply, VOS1, PLL1 to 400/200 MHz, and FLASH
   latency. This is the code every ST example runs first, and it can be ported from the RM0399 sequence.
3. `boards/stm32h747i-disco`: one image built for `thumbv7em-freestanding` (single-precision, per
   skiff's own "weakest core" rule), a link script with code in flash bank 1, both cores entering the
   same reset handler (both option-byte boot addresses set to bank 1, or a bank-2 trampoline), and
   `.shared` in AXI SRAM or SRAM4. The M7 maps it **uncached** through the MPU.
4. `arch/pmsa`: `capacity()` taken from MPU_TYPE.DREGION at run time, or the minimum over the image's
   cores.
5. A boot target in `scripts/boot-test.sh` that runs cortical headless with `--deterministic` and
   `--seed`, beside the QEMU machines. Then the milestone 1–4 and 6 scenarios run unchanged on the
   H747 shape.
6. Note **FINDINGS 10** (`__aeabi_memmove` missing from `sysl.softint.aeabi`). An H747 board link
   will meet it just as the MPS2 links did, so it should be fixed in sysl first.
7. For milestone 9's second half (two images, caches on), cortical's `--dcache-model` (§1.5) is what
   makes "clean before giving, invalidate after taking" testable before the board.

---

## 7. Milestones

Each milestone ends in something the user runs. The estimates are **agent working time** for this
org's flow (one bounded item per agent, a gate per landing), counted in agent-days of roughly 6–8
hours. They include the tests the rules require, which are about a third of each.

| # | the user runs | contents | estimate |
|---|---|---|---|
| **0 — skeleton** | `cortical run --board pico2 blink.elf --headless` prints a hand-written sysl program's semihosting output | the repo, the board-description framework (cores, regions, devices, coprocessors, wiring, panel), the ELF and UF2 loaders (§9.2), the bus with regions and the RP atomic aliases (§9.1), the register types generated from `RP2350.svd`, and the `svd` tool's new bitfield output plus `<dim>` and clusters (in `sysl-lang/svd`; the RP2040 and RP2350 SVDs already pass its constants mode, which `sysl-lang/rp2040` and `sysl-lang/rp2350` are generated by), a first decoder | 3–4 |
| **1 — Pico 2, one core, UART** | pico-examples `hello_serial` prints on a terminal; `blink` (built for `pico2`) blinks GP25's LED in the window; a sysl program built with the SDK prints and blinks; each matches the real board (§9.8) | the complete integer Thumb-2 + IT + DSP, FPv5-SP, the v8-M additions in Secure-only form (§2.7), exceptions/NVIC/SCB/SysTick/PMSAv8, the fast table and decode cache, the three coprocessors, the boot ROM mapped for its function table, RESETS/CLOCKS/XOSC/PLL/TICKS/PSM/WATCHDOG, IO_BANK0/PADS/SIO GPIO, TIMER0/1, UART0 to stdio, a pty and tcp, virtual time, the SDL3 panel with the Pico 2 drawn, the QEMU differential harness (`mps2-an505`'s M33) for the integer, DSP and FP classes, then the benchmark (§2.6) | 8–10 |
| **2 — Pico 2, both cores** | pico-examples `multicore/hello_multicore` and `multicore_fifo_irqs`, both M33s on separate threads; a two-core spin-lock hammer that counts exactly | core 1 started in the real boot ROM and launched through the FIFO protocol (§9.6), the second core's view, SIO FIFOs and their IRQs, hardware spinlocks (E2 behind an option), doorbells, the global exclusive monitor (§1.5), `WFE`/`SEV` across cores, the time window and deterministic mode | 3–4 |
| **3 — Pico + breadboard kit** | `picokit` unmodified, with the screen, touch, joystick, buttons, LEDs and buzzer on the panel; 52Pi's own demo (`geeekpi/pico_breakboard_kit`: FreeRTOS SMP and LVGL on both M0+s, the WS2812 over PIO, the GT911, the joystick) unmodified; the oracle is the user's real kit (§10.6) | ARMv6-M (§2.8) 1.5–2; the RP2040 chip over the RP2350's devices (§10.1) 2–3; PL022 SPI 1; the ST7796 command model with ILI9488 as a variant, and the 320×480 glass 2; the DW I²C master and the GT911 1.5–2; ADC and the joystick 0.5; PWM and the buzzer to host audio 1–1.5; **PIO 4–6**, which the RGB LED needs (§10.4) | 13–18 |
| **4 — skiff on the Pico 2** | skiff's milestone 1–4 and 6 scenarios on the Pico 2's shape, in a window or headless in skiff's gate | cortical's side: semihosting halt, `--expect`, PMSAv8 and `PSPLIM` edge cases skiff reaches, the E2 option exercised, the pty back-end (after the `cstd` additions); **skiff's side is §6.1's request** (its milestone 5), estimated by them | 2–3 (cortical) |
| **5 — H7: M7 + M4 + UART** | ST's `GPIO_EXTI` and `UART_WakeUpFromStopUsingFIFO` on the M7; the three HSEM examples and `FreeRTOS_HwSemaphoreCoreSync` on both cores; a sysl REPL from `sysl-lang/repl` on USART1 | what the Pico 2 did not already build: the `V7EM` kinds, PMSAv7 (16 and 8 regions), FPv5-DP at `.F64` and the FP64 differential classes, the M7's caches and TCMs as §2.1 says, the H7's RCC/PWR/FLASH/SYSCFG/GPIO/EXTI/USART1/HSEM, CM4 gating and Stop, the local-only exclusive model, the DISCO drawn on the panel | 7–9 |
| **6 — H7 display** | ST's LCD_DSI video and command-mode examples, the six DMA2D examples and `FPU_Fractal` on the 800×480 area | FMC SDRAM, LTDC, DSI host + wrapper, OTM8009A and NT35510, DMA2D, MDMA if an example needs it, TIM/DMA as the examples need, headless screenshots and the frame tests | 6–8 |
| **7 — skiff on the H7** | skiff's milestone 1–4 and 6 scenarios on the H747 shape | cortical's side: the H7 exclusives model and its test (§1.5), PMSAv7 edge cases; **skiff's side is §6.2's request**, estimated by them (the AN521 port suggests 3–5 of their agent-days) | 2–3 (cortical) |

**Total for cortical: about 44–59 agent-days**, plus skiff's share; through milestone 4 (both Pico
boards and skiff on the Pico 2) it is 29–39. Milestone 3 is 9–12 without PIO, if the RGB LED is
deferred. The benchmark after milestone 1 says how far §2.6's target is met on the M33. Before
milestone 6, the frame-heavy one, the M7's figure decides whether a performance item
(superinstructions, flag liveness) goes in first: below 100 MIPS, it does.

**Later, costed for planning**:

| item | basis | estimate |
|---|---|---|
| **micro:bit v1** (nRF51822, M0) | a `V6M` kind exists after milestone 3; the nRF51's UART, GPIO, GPIOTE, TIMER/RTC, the 5×5 LED matrix and two buttons on the panel; QEMU's `microbit` machine is a second oracle | 4–5 |
| **micro:bit v2** (nRF52833, M4F) | a `V7EM` kind with FPv4-SP exists after milestone 5; the nRF52's UARTE (EasyDMA), GPIO, TIMER/RTC, the LED matrix, buttons, the speaker over PWM; the microphone and radio are out | 4–6 |
| **CYW43439 on a Pico W / Pico 2 W** | the user's Pico 2 is a W, and `sysl-blink`, `repl` and `picokit` call `cyw43_arch_init` (§9.8, §10.6): PIO-driven gSPI (after PIO), the firmware upload accepted, enough of the chip's bus protocol for init and the LED GPIO; joining a network is out | 5–8 |
| **RP2040/RP2350 USB device controller** | most org Pico programs use `pico_enable_stdio_usb`; the RP-specific controller (not DWC2) plus the bridge built for the H7's USB item below | 4–6, plus the bridge |
| **Non-secure state** (`security: Full`) | §2.7: both worlds' banked state, SAU/IDAU, secure gateways, the integrity signature on entry | 4–6 |
| **The boot ROM's own boot path** | core 0 from reset through the real ROM: OTP, the QMI, partition tables, IMAGE_DEF, signature checks (§9.2) | 4–6 |
| **HSTX, the RP2350's DVI output** | the DVI examples on the panel | 3–4 |
| touch (I2C4 + FT6x06) | §3.1 | 1–1.5 |
| SDRAM/QSPI/SD beyond the display | QUADSPI memory-mapped + indirect (MT25TL01G), SDMMC1 + an image file | 3–4 |
| audio (SAI1 + WM8994 over I2C4) | SAI's FIFO and frame timing to host audio through `sysl-lang/miniaudio`; the WM8994's register file accepted, volume and routing honoured | 4–5 |
| **USB (OTG_HS/FS for tinyusb)** | the org's `sysl-lang/tinyusb` already has an `stm32h7` port (Synopsys DWC2), so cortical models only the **DWC2 core in device mode**: GOTGCTL, GAHBCFG, GUSBCFG, GRSTCTL, GINTSTS/GINTMSK, GRXSTSP, GRXFSIZ, the TX FIFO sizes, DCFG/DCTL/DSTS, the DIEPCTLx/DOEPCTLx/…TSIZ/…INT sets, the FIFO windows, and the interrupt. The host side is a **small USB host inside cortical**: it enumerates the emulated device through the DWC2 model and bridges each class to the Mac: CDC-ACM to a pty, MSC to a disk-image file, HID to an on-screen log. macOS has no USB/IP client and no way to accept a virtual device without a driver, so a bridge is the honest shape | DWC2 device model 3–4, host enumerator + CDC bridge 2, MSC 1: **6–7** |
| wifi | module TBD | — |
| GDB stub (remote serial protocol over TCP, per core) | very useful for firmware debugging | 2 |
| `--dcache-model` | §1.5 | 2–3 |
| a JIT | AArch64 code generation per block behind §2.5 | 15–25 |

---

## 8. Risks and language gaps

### 8.1 Risks

1. **The M7's speed target.** An interpreter will not reach 400 MHz-class M7 throughput, so a UI that
   is smooth on the board will be choppy in cortical until the JIT exists. §2.6 says what to expect,
   and the benchmark decides.
2. **Exclusives across cores.** What the H747's interconnect does with an exclusive store to shared
   SRAM (succeed without excluding, or fail) has to be read from RM0399 and the Cortex-M7 TRM, and
   ideally run on the board. Getting it wrong either way hides a skiff bug or invents one. The option
   in §1.5 runs both readings until it is settled.
3. **Coherence that is too good.** Caches as no-ops hide every missing cache operation. `--dcache-model`
   closes that gap and is costed. Until it exists, a "passes in cortical" on M7-cached data says nothing.
4. **The display stack is undocumented below the HAL.** The DSI host is a Synopsys core whose
   registers ST documents only in RM0399's DSI chapter, and the panel vendor command sets
   (OTM8009A, NT35510) are known mostly through ST's component drivers. The model is built to make
   ST's BSP succeed and to show what it sends, so a sequence the BSP never sends may be modelled wrong.
5. **RM0399 and UM2411 were not read for this note** (both fetches timed out). The facts here come from
   ST's BSP, the CubeH7 tree, ST forum answers and memory. Every † offset is generated from the SVD
   rather than trusted, and the chapter numbers still need filling in.
6. **Build glue for ST's examples.** The examples are IDE projects. A hand-made Makefile is new code
   between ST and cortical, and an example that fails could be the glue's fault rather than cortical's.
   Keep the glue minimal, and cross-check one example's ELF against an STM32CubeIDE headless build once.
7. **The `svd` tool's gaps.** It refuses `<dim>` and clusters today, and the H7 SVD uses them. Its new
   bitfield mode is a prerequisite for every device.
8. **E-cores.** A core thread scheduled on an efficiency core runs at a fraction of the speed, so
   measured MIPS swing (memory note "Timings on this Mac"). The threads need a QoS class, or MIPS
   figures are noise.
9. **Peripheral breadth creep.** Each new ST example tends to pull in one more device (MDMA, a timer
   channel, DMAMUX request generators). The milestone tables fix the set; anything else is a new item.
10. **The boot ROM images' licences.** cortical runs Raspberry Pi's real boot ROMs (§9.2, §10.1).
    `raspberrypi/pico-bootrom-rp2350` and `raspberrypi/pico-bootrom-rp2040` both report their licence
    to GitHub as "Other". Both are read before anything is vendored. If
    redistribution is not allowed, cortical fetches the ROM at build time or reads it from a file the
    user supplies (`picotool save` from their own board is a source too). The ROM build must also match
    the user's silicon revision (A2 vs A4 RP2350), which `picotool info -a` reports.
11. **The RP2350's coprocessors and errata are documented only in the datasheet.** The DCP's exact
    results are checked against the SDK's own `pico_double` tests run on the real board, and
    erratum E2 is modelled behind an option, not by default.
12. **The user's Pico boards are W variants.** The Pico 2 is a Pico 2 W, whose LED is on the CYW43439
    (`pico-scratch/CLAUDE.md`), and `picokit` today wants a Pico W (§10.6). Until the CYW43 item
    exists, the oracle programs are the ones that use a GPIO LED and UART0.
13. **PIO timing.** A PIO state machine is a small CPU that runs at `clk_sys`/divider. The WS2812
    waveform and gSPI are judged by their decoded output (the colour, the bytes), not by edge timing,
    so cortical runs PIO at instruction granularity in virtual time and does not model pin
    synchronisers.

### 8.2 Language and library gaps

Found while designing. This note was written without running the compiler (the brief allows no
builds), so each is either a **known absence** or a **probe to run** before the item that needs it.
None is designed around. Each goes to the `sysl` session (or `cstd`) as the org's rules say.

| # | what | evidence | what cortical needs | kind |
|---|---|---|---|---|
| G1 | **WITHDRAWN: not a gap.** A guaranteed tail call between functions, for a tail-calling interpreter (each handler ends by jumping to the next one's, the shape wasm3 and CPython 3.14 use). The claim was made from `@tailrec` (`attributes.md`), which is about a function's call to itself. sysl has `become`, a guaranteed tail call between functions (LLVM `musttail`), and `sysl-lang/forth`'s interpreter is built on it | the org's own program | a tail-calling dispatch as the alternative to §2.3's `match` loop, chosen by measurement | withdrawn |
| G2 | **`posix_openpt`/`grantpt`/`unlockpt`/`ptsname` missing** from `cstd` (`cstd/POSIX.md`: "missing") | read | the pty back-end of §3.2 | **library gap**: add them to `cstd` (its POSIX-complete project) |
| G3 | Is a capture-free named function stored as `&Fn` a plain code pointer, or a counted box? (`types.md`: "no separate function-pointer concept") | probe | not needed by the `match` design; matters if a table of 450 handlers is ever wanted, and for G1's alternative | probe |
| G4 | Can reflection (`T::Fields`) derive a register block's offset-to-field `read`/`write` dispatch, so a device is written once against its struct? | probe | otherwise every device repeats its layout as a `match` on offsets; the generator can write that, but a language that can derive it is the pleasant answer | probe |
| G5 | AArch64 FPCR/FPSR as system registers in a **hosted** build (the compiler supports AArch64 sysregs) | probe | §2.1's host-FPU mapping of FPSCR modes and flags | probe |
| G6 | Thread QoS / P-core placement in `sysl.threads` (`pthread_set_qos_class_self_np`) | probe | §8.1 risk 8 | probe; library gap if absent |
| G7 | An `Atomic[u32]` (and `Atomic[u8]`, `Atomic[u16]`) view of a raw `*u8` + offset (guest RAM as host memory, accessed with acquire/release orderings for the M4 in §1.5, and compare-exchanged at byte width for the RP2350's global exclusive monitor, §9.3) | to probe | the parallel memory model | to probe |
| G8 | `__aeabi_memmove` missing from `sysl.softint.aeabi` (skiff FINDINGS 10, open) | known | skiff's Pico 2 and H747 links (§6.1, §6.2 item 6) | **bug, already filed by skiff** |
| — | **Checked and not gaps** (the org's programs already do them): host audio fed from another thread, and a 64-bit atomic counter shared across threads. `musicbox-miniaudio/…/ring.sysl` keeps a `&sync Ring` with `Atomic[u64]` indices that miniaudio's data callback drains while the render thread fills it. The buzzer (§10.5) and the shared timers' virtual time (§9.5) take the same shape | `ring.sysl:19–69` | — | not a gap |

Each "to probe" becomes a small program and its exact `sysl build` output before the milestone that
needs it, and either a closed line here or a report to the `sysl` session. Reading a reference page is
not a probe: before a new row is added, the reference pages under `sysl/docs/content/reference/` and
the org's programs are grepped for the feature, as G1's withdrawal shows.

---

## 9. Board: Raspberry Pi Pico 2 (RP2350)

The first board. Two M33s (§2.7) at 150 MHz, 520 KB of SRAM, 4 MB of QSPI flash on the Pico 2 and the
Pico 2 W. Every address below is from the SDK's generated headers, which come from the same SVD that
cortical's register types are generated from (§3.0), so the note's numbers are a summary, not the
source. Datasheet chapters are cited by title.

### 9.1 The memory map

From `src/rp2350/hardware_regs/include/hardware/regs/addressmap.h:23–109` (datasheet, "System bus",
"Address map"):

| region | base | what cortical does |
|---|---|---|
| boot ROM, 32 KB | `0x0000_0000` | the real ROM image, read-only (§9.2) |
| XIP flash | `0x1000_0000` (cached), aliases at `0x1400_0000`, `0x1800_0000` (maintenance), `0x1c00_0000` | one flash image behind every alias; the cache maintenance alias accepts writes; QMI timing is not modelled |
| XIP cache as SRAM, 16 KB | `0x13ff_c000` | RAM when the cache is disabled by `XIP_CTRL` |
| SRAM0–7, 512 KB striped | `0x2000_0000`–`0x2007_ffff` | host memory, one region |
| SRAM8/9 (scratch X/Y), 4 KB each | `0x2008_0000`, `0x2008_1000` | host memory, where the SDK puts the cores' stacks |
| APB peripherals | `0x4000_0000`–`0x4016_ffff` | devices (§9.3–§9.7); the rest refused with a named diagnostic |
| AHB peripherals | `0x5000_0000` (DMA), `0x5010_0000` (USB), `0x5020_0000`/`0x5030_0000`/`0x5040_0000` (PIO0–2) | DMA when an example needs it; USB and PIO are later items (§7) |
| SIO | `0xd000_0000`; Non-secure alias `0xd002_0000` | per-core device (§9.3); the alias as §2.7 says |
| PPB | `0xe000_0000`; Non-secure alias `0xe002_0000`; EPPB `0xe008_0000` | the core's own SCB/NVIC/SysTick/MPU/SAU |

**The atomic register aliases are on the hot path.** Every APB and AHB peripheral register also
appears at `+0x1000` (XOR), `+0x2000` (SET) and `+0x3000` (CLR) (datasheet, "System bus", "Atomic
register access"), and the SDK's `hw_set_bits`/`hw_clear_bits` use them everywhere. The bus decodes
the alias once per access and hands the device a read-modify-write, so a device is written once
against plain reads and writes. The RP2040 has the same scheme (§10.1).

### 9.2 The boot path

The user's images are UF2 and ELF. Three ways to start, and the one chosen:

1. **The real ROM from reset on both cores.** Most faithful, and most work: core 0's ROM path reads
   OTP, probes the flash through the QMI, walks partition tables, finds the image's `IMAGE_DEF` block,
   checks signatures if OTP asks, and the RCP checks run throughout (datasheet, "Bootrom").
2. **No ROM at all.** Breaks the SDK: it calls ROM functions through the ROM's lookup table
   (`rom_func_lookup`, `rom_reset_usb_boot`, the flash routines), and core 1 is released by ROM code.
3. **Chosen: the real ROM mapped, core 0 entered as a debugger enters it, core 1 in the ROM.** The
   loader writes a UF2's blocks (family `rp2350-arm-s`) or an ELF's `PT_LOAD` segments at their load
   addresses into the flash and SRAM images. Core 0 then starts at the ELF entry, `_entry_point`,
   which is the SDK's own supported debugger entry. `crt0.S:399–432` enables the RCP, seeds the canary
   salts if the ROM has not, zeroes `MSPLIM` and vectors through the image's own table. Core 1 starts
   at the ROM's reset vector and runs the ROM's core-1 path, which waits on the FIFO for the launch
   sequence (§9.6), so the release is the real ROM's code. For a UF2 with no ELF, core 0 enters at
   the image's vector table in flash, as the ROM would after finding it.

   The cost of option 3 is a list of ROM postconditions core 0 does not get: the clocks as the ROM
   leaves them (the SDK reprograms them anyway), the QMI set up for the flash (not modelled), the
   boot-lock and the RCP state. These are a table in the board description, each checked against the
   real board by reading the registers at `_entry_point` with the debug probe, or with a first
   instruction that dumps them over UART. Option 1 is a costed later item (§7).

### 9.3 SIO

The single-cycle I/O block, one view per core (`regs/sio.h`; datasheet, "Processor subsystem",
"SIO"):

- `CPUID` (`0x000`) answers 0 or 1 by which core reads it.
- GPIO: `GPIO_IN` (`0x004`), `GPIO_OUT` (`0x010`) and the OE registers with their SET/CLR/XOR
  forms, plus the high bank. The GPIO coprocessor (§2.7) writes the same state.
- **The inter-core FIFOs** (`FIFO_ST` `0x050`, `FIFO_WR` `0x054`, `FIFO_RD` `0x058`): one per
  direction, 32 bits wide and **4 entries deep on the RP2350** (8 on the RP2040; `pico_multicore/
  include/pico/multicore.h:133`). `FIFO_ST` reports VLD/RDY and the sticky WOF/ROE error flags. A
  non-empty FIFO raises `SIO_IRQ_FIFO` (IRQ 25, `regs/intctrl.h:41`) on the reading core.
- **Spinlocks**: 32 (`NUM_SPIN_LOCKS`, `platform_defs.h:32`) at `0x100`, plus `SPINLOCK_ST`
  (`0x05c`). **Erratum RP2350-E2** lets writes to nearby SIO registers release a lock, so the SDK
  defaults to **software** spin locks on the RP2350 (`hardware_sync_spin_lock/include/hardware/sync/
  spin_lock.h:13`: "default=1 on RP2350 due to errata E2"), built on `LDAEXB`/`STREXB` in SRAM. So
  the cross-core exclusive monitor (§1.5) is what the SDK's locks run on, and the hardware spinlocks
  are what skiff may choose. cortical models the spinlocks correctly by default and E2 behind
  `--erratum rp2350-e2`, so a lock E2 would break can be shown to break.
- **Doorbells**: 8 (`NUM_DOORBELLS`, `platform_defs.h:46`) at `0x180`, raising `SIO_IRQ_BELL`
  (IRQ 26).
- **The interpolators** (`INTERP0`/`INTERP1` at `0x080`): per core, pure functions of their
  registers, cheap. Built when an example needs them (the SDK's own `interp` examples are the test).
- **No integer divider.** The RP2040's SIO divider (`DIV_UDIVIDEND` … `DIV_CSR`, `0x060`–`0x078`
  in `src/rp2040/…/regs/sio.h:336–421`) has no counterpart in the RP2350's `sio.h`: the M33s divide
  with `SDIV`/`UDIV`. The divider is built for the RP2040 (§10.1).
- The RISC-V platform timer (`MTIME`, `0x1a4`) and soft IRQs (`0x1a0`) belong to the Hazard3 cores
  and are refused; the TMDS encoder (`0x1c0`) is built with HSTX, later.

### 9.4 Clocks, resets, power: as far as the SDK's start-up needs

The SDK's `runtime_init` takes the chip out of reset and sets the clocks, and each step polls for a
condition. cortical makes every condition true at the moment silicon would report it in virtual time,
so a program that polls the wrong bit hangs in cortical as on the board:

- **RESETS** (`0x4002_0000`): `RESET` bits hold a block in reset; `RESET_DONE` follows after the
  write. A register access to a block in reset is refused with a named diagnostic, because on silicon
  it hangs the bus (datasheet, "Resets").
- **XOSC** (`0x4004_8000`): `CTRL` enable, `STATUS.STABLE` after `STARTUP.DELAY` × 256 cycles of the
  12 MHz crystal (the Pico 2's crystal).
- **PLL_SYS / PLL_USB** (`0x4005_0000`, `0x4005_8000`): `CS.LOCK` once `PWR` and `FBDIV_INT` are
  set; the output frequency is computed from `REFDIV`, `FBDIV` and the post dividers.
- **CLOCKS** (`0x4001_0000`): each clock's `CTRL`/`DIV`, and `SELECTED` reporting the glitchless
  mux's choice. The tree's result drives each core's virtual clock (`clk_sys`) and each device's
  (`clk_peri` for UART and SPI). The frequency counter (`FC0`) answers from the tree, since some
  examples print `clock_get_hz` against it.
- **TICKS** (`0x4010_8000`): the RP2350's tick generators. TIMER0/1 and the watchdog count only once
  their generator is enabled with the right cycle count, which is exactly the step a hand-written
  start-up forgets.
- **PSM**, **POWMAN**, **WATCHDOG**: stored. Watchdog `SCRATCH` registers survive a watchdog reset,
  and the reset itself restarts the machine through §9.2's path.

### 9.5 UART0, TIMER and GPIO

- **UART0** (`0x4007_0000`, IRQ 33) is an Arm PL011: `UARTDR`, `UARTFR`, `IBRD`/`FBRD`, `LCR_H`,
  `CR`, the 32-entry FIFOs with their level interrupts, `IMSC`/`RIS`/`MIS`/`ICR`, `DMACR` (datasheet,
  "UART"; the PL011 TRM). Its byte rate follows `clk_peri` and the divisors in virtual time. It goes
  to stdio, a pty or a TCP port exactly as §3.2's USART1 does. UART1 is the same device at
  `0x4007_8000`. The RP2040 reuses it (§10.1).
- **TIMER0/1** (`0x400b_0000`, `0x400b_8000`; datasheet, "Timers"): a 64-bit microsecond counter
  computed from virtual time, `TIMEHR`/`TIMELR` with the read-low-latches-high rule, `TIMERAWH/L`
  without the latch, four `ALARM`s each with its own IRQ (TIMER0's at IRQ 0–3), `ARMED`, and
  `INTR`/`INTE`/`INTF`/`INTS`.
- **GPIO** (`IO_BANK0` `0x4002_8000`, `PADS_BANK0` `0x4003_8000`; datasheet, "GPIO"): `GPIOn_CTRL`
  with `FUNCSEL` (SIO is 5, `regs/io_bank0.h:146`) and the over-rides, `GPIOn_STATUS`, the per-core
  interrupt registers with edge latching (`IO_IRQ_BANK0`, IRQ 21). **Pads come up isolated on the
  RP2350**: `PADS_BANK0` `ISO` (bit 8, `regs/pads_bank0.h:38`) must be cleared before a pad drives,
  and the SDK's `gpio_set_function` clears it. A pad with `ISO` set does not drive in cortical either,
  since that is the one RP2040 habit that leaves an RP2350 LED dark. Pull-ups and pull-downs decide an
  undriven input, which is how the panel's buttons are read.

### 9.6 The multicore launch protocol

Core 1 sits in the ROM after reset. `multicore_launch_core1_raw` (`pico_multicore/multicore.c`)
pushes the sequence `0, 0, 1, vector_table, stack_pointer, entry` into core 1's FIFO one word at a
time, and core 1's ROM echoes each word back. A wrong echo restarts the sequence from the start, and
each `0` lets core 1 drain its FIFO and is followed by `SEV` (datasheet, "Bootrom", "Launching code
on processor core 1"). With core 1 running the real ROM (§9.2), cortical runs this protocol rather
than imitating it, and milestone 2's test is that `hello_multicore` launches core 1 exactly when
the board does: after the sixth echo, with `VTOR`, `SP` and `PC` as sent.

### 9.7 PIO, costed as an optional later item for the Pico 2

Three PIO blocks of four state machines (`0x5020_0000`, `0x5030_0000`, `0x5040_0000`; datasheet,
"PIO"), each machine a small CPU: nine instructions, a 32-word shared instruction memory, the X/Y
scratch registers, the shift registers with autopush/autopull, 4-deep TX/RX FIFOs (joinable to 8),
side-set, the clock divider, `IRQ` flags, `WAIT` on pins and IRQs, and the pin mappings through
`IO_BANK0`. In cortical a state machine is a fourth kind of thing beside the cores, the devices and
the coprocessors: an interpreter stepped in virtual time at `clk_sys`/divider, run on the device
thread. **4–6 agent-days**, the same code for the RP2040's two blocks. The Pico 2 does not need it
for milestones 1–2. The breadboard kit's RGB LED does (§10.4), so it is costed inside milestone 3.
The CYW43's gSPI (the W boards' radio and LED) is its second consumer (§7, later).

### 9.8 The oracle

**The user's real board**, with the same image flashed (`picotool load -f`, `pico-scratch/CLAUDE.md`),
and these programs built **unmodified** with the SDK in `~/dev/sysl-lang/pico-scratch/pico-sdk`:

- pico-examples `hello_world/serial` (`hello_serial`), `blink`, `multicore/hello_multicore`, and
  `multicore/multicore_fifo_irqs`, built for `PICO_BOARD=pico2`;
- the org's own sysl Pico 2 programs: `pico-scratch/sysl-blink` and `repl`, `ogol-pico2`,
  `solder-pico2`, each run once the devices it uses exist. Most use USB stdio and the CYW43 LED
  (`pico-scratch/CLAUDE.md`), so which ones run at milestone 1 is recorded per program, not assumed.

What matches is judged by output: the UART bytes, the GPIO levels over time, and the registers dumped
by a test image. Two facts about the user's board decide how. **It is a Pico 2 W**, whose LED is on
the CYW43439 rather than GP25 (`pico2_w.h`: "no PICO_DEFAULT_LED_PIN - LED is on Wireless chip"), so
a `pico2` build's GP25 blink needs an LED on GP25 or a meter to be seen on the board. **UART0 on
GP0/GP1 needs a 3.3 V USB-serial adapter** (or a Raspberry Pi Debug Probe) to reach the Mac; the board's
own USB port carries USB stdio, which cortical does not model until the USB item.

---

## 10. Board: Pico + the GeeekPi 3.5" breadboard kit (RP2040)

The user's board is GeeekPi's "Expansion Module 3.5inch" for the Raspberry Pi Pico (Amazon
B0C98G37PW), the GeeekPi edition of 52Pi's EP-0172, with an original Pico (RP2040, two M0+s) plugged
in. `sysl-lang/picokit` is the org's program for it. The pin map is from `picokit/README.md:31–37`,
which agrees with the 52Pi wiki:

| part | wiring |
|---|---|
| 3.5" 320×480 TFT, ST7796, SPI0 | CLK GP2, DIN (MOSI) GP3, CS GP5, DC GP6, RST GP7; no MISO |
| capacitive touch, **GT911**, I2C0 | SDA GP8, SCL GP9; no INT or RST pin listed, so it is polled |
| joystick | X on ADC0 (GP26), Y on ADC1 (GP27) |
| buttons | BTN1 GP15, BTN2 GP14, both switching to ground (`picokit/README.md:89–92`) |
| LEDs | D1 GP16, D2 GP17 |
| RGB LED, **WS2812** | GP12 |
| buzzer | GP13 |

### 10.1 The RP2040, as differences from the RP2350

The RP2040 shares most device IP with the RP2350 (PL011 UART, PL022 SPI, the DesignWare I²C, the
ADC, PWM, PIO, the atomic aliases, the multicore protocol), so its chip description is mostly the
same devices at different addresses (`src/rp2040/hardware_regs/include/hardware/regs/addressmap.h:23–78`;
the `sysl-lang/rp2040` package is generated from the same SVD). What differs:

- **Cores**: two M0+s at 133 MHz, `V6M` (§2.8), no FPU, no coprocessors, no TrustZone, no
  exclusives. The SDK's spin locks are the hardware spinlocks here (E2 is an RP2350 erratum).
- **The SIO divider**, which the RP2350 dropped: `DIV_UDIVIDEND`/`UDIVISOR`/`SDIVIDEND`/`SDIVISOR`/
  `QUOTIENT`/`REMAINDER`/`CSR` at `0x060`–`0x078` (`src/rp2040/…/regs/sio.h:336–421`). A result is
  ready 8 cycles after the divisor is written, `CSR.READY` and `DIRTY` report it, and the SDK's
  `__aeabi_idiv` and its interrupt save/restore rely on both, so they are exact in cycles.
- **The FIFOs are 8 deep** (`multicore.h:133`).
- **The memory map**: a 16 KB ROM; XIP at `0x1000_0000` behind the DW_apb_ssi (`XIP_SSI`
  `0x1800_0000`) rather than the QMI; 264 KB of SRAM at `0x2000_0000`, striped, plus the non-striped
  aliases at `0x2100_0000`; APB devices at their RP2040 offsets (UART0 `0x4003_4000`, SPI0
  `0x4003_c000`, I2C0 `0x4004_4000`, ADC `0x4004_c000`, PWM `0x4005_0000`, TIMER `0x4005_4000`);
  PIO0/1 at `0x5020_0000`/`0x5030_0000`.
- **Boot**: the first 256 bytes of flash are `boot2`, which the ROM copies to SRAM and runs to set
  up the SSI. With §9.2's debugger entry, cortical never runs `boot2`. The SSI's registers are
  stored, flash reads just work, and a program that reprograms the SSI for XIP is accepted.
- **The ROM is called much more**: the RP2040's ROM holds the soft-float and soft-double routines
  and the fast `memcpy`/`memset` that the SDK's `pico_float`, `pico_double` and `pico_mem_ops` call
  through the ROM tables. So the real RP2040 ROM (`raspberrypi/pico-bootrom-rp2040`, §8.1 risk 10) is
  mapped, and core 1 starts in it as on the RP2350.
- **Clocks**: the same XOSC/PLL/CLOCKS shape with no TICKS block; the timer's 1 µs tick comes from
  `WATCHDOG_TICK`. One 64-bit TIMER with four alarms. No POWMAN; `VREG_AND_CHIP_RESET` instead.
- **Pads** have no `ISO` bit.

**Cost: 2–3 agent-days** on top of the RP2350, most of it the divider, the ROM's float tables, and
the address and IRQ tables from the SVD.

### 10.2 PL022 SPI and the ST7796

- **PL022** (SPI0 `0x4003_c000`; the PL022 TRM): `SSPCR0`/`CR1` (frame format, data size, clock
  rate), `SSPCPSR`, `SSPDR` into 8-entry TX/RX FIFOs, `SSPSR` (`TFE`, `TNF`, `RNE`, `RFF`, **`BSY`**),
  the interrupts, `DMACR`. Bytes shift at the configured bit rate in virtual time, and **`BSY` stays
  set until the last bit leaves**, because picokit's `drain` waits on `BSY` rather than on an empty
  FIFO (`picokit/README.md:128–129`, and `sh.sysl.rp2040blocks`). **1 agent-day.**
- **The ST7796 command model**: a byte stream with DC sampled per byte and CS held as a GPIO across
  a command and its parameters (`picokit/README.md:123–126`). `RST` low resets it. The commands
  modelled: `SWRESET`, `SLPIN`/`SLPOUT`, `DISPOFF`/`DISPON`, `COLMOD` (16- and 18-bit), `MADCTL`
  (row and column order, exchange, BGR), `CASET`/`RASET`, `RAMWR`/`RAMWRC`, `INVON`/`INVOFF`; the
  `CSCON`-gated extension registers and both gamma curves are accepted and recorded. **The glass's
  polarity is a board fact**: this kit's IPS glass shows the inverse of the controller's default, so
  it wants `INVON` (`picokit/README.md:113–121`), and cortical shows the wrong colours for a program
  that does not send it, as the board does. Delays the datasheet requires after `SWRESET` and
  `SLPOUT` are checked in virtual time and reported if a program writes too early.
- **The ILI9488 as a variant**: same pinout and resolution, a different command set, and 18-bit
  pixels only over SPI. 52Pi say the controller "may vary with production lot"; the user's is an
  ST7796 (`picokit/README.md:119–121`), so ILI9488 is a description choice, not the default.

**2 agent-days** for the controller, its variant and the 320×480 area on the panel.

### 10.3 Touch: the DW I²C master and the GT911

- **The touch controller is a Goodix GT911**. 52Pi's documentation does not name it, but 52Pi's own
  demo (`geeekpi/pico_breakboard_kit`, `lv_port_indev.c`) drives it through `gt911_*` functions with
  16-bit register addresses and the GT911's status register `0x814E` (`lv_port_indev.h`). Its I²C address on this board
  is **`0x5D`** (`GT911_I2C_SLAVE_ADDR` in `lv_port_indev.h`; the GT911 chooses `0x5D` or `0x14` from
  its INT pin's level at reset).
- **The DesignWare I²C master** (I2C0 `0x4004_4000`; datasheet, "I2C"): `IC_CON`, `IC_TAR`,
  `IC_DATA_CMD` with its `RESTART` and `STOP` bits, the TX/RX FIFOs, `IC_RAW_INTR_STAT`, and
  `IC_TX_ABRT_SOURCE` with a NACK when no device answers at the address, since the SDK's
  `i2c_write_blocking` reports exactly that. Bytes move at the configured SCL rate.
- **The GT911 register model**: product ID `"911"` at `0x8140`, firmware and resolution at
  `0x8144`–`0x8149`, the configuration block from `0x8047`, the status `0x814E` (bit 7 "buffer
  ready", low nibble the point count, cleared by writing 0), and point 1 at `0x8150`. The panel
  supplies one touch point from the mouse, so a second point is a later option.

**1.5–2 agent-days.**

### 10.4 Joystick, buttons, LEDs, the RGB LED

- **ADC** (`0x4004_c000`): `CS` (`EN`, `START_ONCE`, `READY`, `AINSEL`, round-robin), `RESULT`
  12-bit, the FIFO; a conversion takes 96 ADC clocks. The joystick's two axes are drawn on the panel
  and dragged, or driven by the arrow keys, and their voltages read as 12-bit values. **0.5
  agent-days.**
- **Buttons** are panel controls pulling GP15/GP14 to ground; with the pads' pull-ups enabled they
  read high when released, as picokit expects. **LEDs** are drawn from GP16/GP17's output levels.
- **The RGB LED is a WS2812, and it needs PIO.** 52Pi's demo drives it with `ws2812.pio`
  (`ws2812_program_init(pio, sm, offset, 12, 800000, true)` in its `main.c`); `picokit` does not drive
  it yet (`picokit/README.md:153`). cortical decodes the waveform on GP12 into 24-bit GRB values and
  draws the LED's colour, so it needs §9.7's PIO, at **4–6 agent-days**. Decoding the pin from a
  bit-banged GPIO waveform would cover a program without PIO, but 52Pi's demo is the oracle, and it
  uses PIO.

### 10.5 The buzzer: PWM to audio

52Pi's demo toggles GP13 as a level (`gpio_xor_mask(0x2000)` in its `main.c`), so the buzzer is
treated as an **active** buzzer: a fixed tone while the pin is high. A program that drives GP13 from
PWM (slice 6, channel B; datasheet, "PWM") is heard at the PWM frequency and duty. If the user's
part turns out to be passive (silent when held high), the description switches to "the pin's
waveform is the sound". The audio goes to the Mac through `sysl-lang/miniaudio`, with the same
ring-into-a-data-callback shape `musicbox-miniaudio` uses (§8.2).

- **PWM** (`0x4005_0000`): eight slices, `CSR` (phase-correct, `DIVMODE`), `DIV`, `CTR`, `CC`, `TOP`,
  the wrap interrupts. **1–1.5 agent-days** with the audio path.

### 10.6 The oracle

**The user's real kit**, running the same image, and two programs built **unmodified**:

- **`picokit`**: the screen, the buttons, the LEDs; and touch, the joystick, the RGB LED and the
  buzzer as picokit grows them (its README lists touch, the joystick and the RGB LED as not done).
- **52Pi's demo**, `geeekpi/pico_breakboard_kit`: FreeRTOS SMP across both M0+s with LVGL, the GT911,
  the joystick's ADC readings on screen, the WS2812 through PIO, the buzzer, the buttons and LEDs.
  It exercises every part of the kit picokit does not yet, so milestone 3 is judged against both.

**One fact may block "picokit unmodified"**: `picokit` links `pico_cyw43_arch_none` and calls
`sh.sysl.pico`'s `init`, which starts the CYW43. So it **wants a Pico W** and fails to link for a plain
Pico (`picokit/README.md:94–104`). If the Pico in the user's kit is a plain Pico, picokit needs the
change its README describes (call `stdio_init_all` directly) before it runs on that board or in
cortical. If it is a Pico W, cortical needs the CYW43 item (§7, later) for `cyw43_arch_init` to
succeed. Which Pico is under the kit decides it, and that is the user's to say.
