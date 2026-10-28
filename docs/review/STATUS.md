# Review findings — status

The 88 findings of the code review in this directory (`cores.md`, `runtime.md`, `peripherals.md`,
`ui-firmware.md`; `SUMMARY.md` ranks them), one row each. The review read the `pico2-riscv` branch,
since merged, so its line numbers have moved. A status is **open**, **fixed in `<sha>`**, **fixed by
slice N** (the machine slices of [machine.md](../machine.md)), **partly fixed** (what is left is
said), or **n.a.** with the reason. Each one marked fixed was checked against the code, not assumed.

| finding | title | status |
|---|---|---|
| cores#1 | the RISC-V core is not wired into any board, and has no bus | partly fixed (core bus) in `aff3dad`: the hart reaches devices through a `riscv.Bus` trait off its RAM window; left: wiring a hart into a board, which waits on device handlers that take a v6m `*Cpu` and on the RP2350 chip layer (GPIO, UART, SIO, ROM) |
| cores#2 | the memory map is process-global state the core reads on every access | fixed by slice 1 (`ce0b959`): `v6m.Memory` holds the map, `Cpu.mem` points at it |
| cores#3 | every new `Op` must be written in five places | partly fixed in `35449b8`: the hand list is gone (`configure_instruction_timing` walks `Op::Variants`), `instruction_cycles` is the one cost table and `costed(op)` derives from it whether a shared handler charges cycles; left: the handler table is still an exhaustive `match` — a `for const c in Op::Variants` table needs `Op::Make(c, ())` as a value argument, which sysl 0.7.8 refuses (sysl finding) |
| cores#4 | hand-unrolled const-generic dispatch ladders, doubled by `TIMED` | fixed in `35449b8`: every ladder is a `for const` (B<cond>, fused, wide CMP, IT, FP, DP imm/logic/reg×shift, bit, extend, the load families) |
| cores#5 | `TIMED`, `V8`, `SINGLE` are `const u32` used as booleans | fixed in `35449b8` (`const …: bool`; FP's precision `D` left a `u32`) |
| cores#6 | the timing switch checked at decode in three spellings; IT re-dispatch | partly fixed in `35449b8`: `CYCLE_ACCOUNTING` deleted; one spelling (`handler_for`, `fused_handler_for`, `cmp_w_bcond_handler`, `pred_handler` all take the core); `h_pred[K, L, TIMED]` no longer tests the timing switch at run time; left: it still re-selects its inner handler per run — the 16-byte record has no room for a second handler pointer |
| cores#7 | the RISC-V core is RP2350-specific while claiming to be generic | fixed in `aff3dad`: a `Chip` record holds what the integration fixes, `riscv/rp2350.sysl` is the one that names the RP2350 |
| cores#8 | RISC-V per-instruction cost far from "real-world speed" | partly fixed in `aff3dad`, `6e909be`: cached IRQ line, decoded PMP with a Machine-mode fast path, word RAM access; 1247 → 224 host instructions per guest instruction (bench/README.md); left: a decode cache and the threaded executor v6m has |
| cores#9 | NVIC banks use three indexing schemes | open |
| cores#10 | flash write protection is an extra compare on every store | open |
| cores#11 | sentinel and guard-record arithmetic in place of named concepts | open |
| cores#12 | `Kind`, `Arch` and the `is_*` predicates overlap | open |
| cores#13 | `h_dsp` re-checks DSP at run time against a second source of truth | open |
| cores#14 | duplicated bit utilities and hand-rolled loops the library has | partly fixed in `d435c66` (`t2sem` CLZ is `leading_zeros`); left: `rbit` (waits for sysl 0.7.9's `reverse_bits`), the RISC-V `bitops` loops, the duplicated `sext`/`bits`/`bad`/`st` helpers |
| cores#15 | boilerplate handler bodies | fixed in `35449b8`: `h_step[OP, L, TIMED]` and `h_access[OP, L, TIMED]`, whose `match` on the constant `OP` folds, replace ~85 hand-written handlers |
| cores#16 | RISC-V `execute` mixes decode, validation and commit | fixed in `aff3dad`: one `retire` commit path, faults carry an `Access` instead of a side channel |
| cores#17 | `Stop.Budget` means "running" | open |
| cores#18 | RISC-V `Model` fields that nothing reads | fixed in `aff3dad`: `model.irqs` sizes the interrupt scan; the chip-fixed fields moved to `Chip` |
| cores#19 | Python-generated tests addressed by hard-coded record ranges | fixed in `aff3dad`: each vector carries its mnemonic and the harness selects by it, iterating `MODELS` |
| cores#20 | tests that restate the cost table | open |
| cores#21 | comments that record history rather than invariants | open |
| runtime#1 | no board object: the machine is module globals shared across threads | partly fixed by slices 1–4 (`ce0b959`, `3024687`, `679528a`, `d72cffd`, `a53b6ff`, `9d54348`): memory, devices, scheduler, workers and cores are owned; left: the cores' option globals, SIO FIFOs and spinlocks, the bus cache and `virt.machine` (slice 4 part 2) |
| runtime#2 | devices are a closed enum dispatched by hand, not a trait | open (slice 4 part 2: the `Peripheral` trait) |
| runtime#3 | four schedulers share one set of globals | fixed by slice 3 (`679528a`): one `Scheduler`; `pump_parallel` and `parallel.sysl` gone; `run_on_threads` and `threaded` split |
| runtime#4 | locks guard nothing: `Mutex.new(0)` around separate globals | fixed by slice 3 (`679528a`, `d72cffd`, `a53b6ff`): `Mutex[WorkerControl]`, device state in `Mutex[T]`; the one `Mutex[u64]` owns the board clock it guards |
| runtime#5 | `assemble` mutates globals before it can still fail | fixed by slices 1–2 (`ce0b959`, `3024687`): `plan` checks everything, the apply step cannot fail |
| runtime#6 | peripheral kinds and window sizes duplicated; one wrong error message | partly fixed in `013e796` (the duplicate-I2C refusal names both kinds); the kind table and the double `controller` parse remain |
| runtime#7 | magic register fields where the comments name the field | fixed in `d013662`: `h7` names RCC_GCR (`sh.sysl.stm32h747.rcc`) and BOOT_C1/C2; the SYNCEN, SLOTEN and A/B block layout live in `peripheral/sai` (`synchronous`, `slot_enabled`, `block_index`, `data_register`); DMAMUX request IDs, default clocks and storage pins are named; `dsi.is_read` replaces the worker's kind test |
| runtime#8 | hand-rolled ring buffers where `Ring` exists | open |
| runtime#9 | the DMA bus master is "whichever core last touched audio" | open |
| runtime#10 | conditional locking keyed on a mutable global | fixed by slice 3 (`d72cffd`): `rp.live` is taken on every access, whichever thread |
| runtime#11 | host workflow reaches into virt globals and duplicates the image layout | open |
| runtime#12 | per-core configuration as parallel arrays, copy-pasted core setup | open (part of slice 4 part 2: the cores' option globals) |
| runtime#13 | generic checker contains part-specific rules | open |
| runtime#14 | boolean-trap positional constructors | open |
| runtime#15 | wall-clock, busy-spinning tests | open |
| runtime#16 | `start_workers` mutates state before it validates | fixed by slice 3 (`679528a`): the divisor check runs before anything is set |
| runtime#17 | asymmetric worker entry points (`cpu_worker0(ignored)`) | open |
| runtime#18 | disk-queue backpressure becomes a guest-visible write error | open |
| runtime#19 | cache-line padding by hand, sized for the wrong machine | open (`BusCache` still pads to 64) |
| runtime#20 | sysl idiom (redundant suffixes, hand `min`/`max`, qualified calls) | open |
| runtime#21 | stale or misleading comments, dead paths | partly fixed (slice 3 deleted `parallel.sysl` and its "legacy" comments); the rest open |
| peripherals#1 | registers are magic offsets and masks; bitfields never used | fixed in `d013662`, `6252927`, `5d04466`: SDMMC, SAI, DSI, DMA, DMAMUX and the STM32 SPI name every offset and field from `sh.sysl.stm32h747` 0.1.1 (SVD-generated, the one definition); WM8994, FT6x06, GT911, DS3231 and the PL022 engine have named constants from their data sheets. Named constants rather than bitfield structs, so the field positions have one source (the generated package) instead of a hand-written second copy |
| peripherals#2 | no common peripheral interface | open (slice 4 part 2) |
| peripherals#3 | the STM32H747 MCU is pasted into three board files | open |
| peripherals#4 | MCU facts in board files; pin routing written twice | open |
| peripherals#5 | the board/MCU schema is inconsistent with itself | open |
| peripherals#6 | two ownership conventions and three `reset` styles | open |
| peripherals#7 | five or more hand-rolled ring buffers | open |
| peripherals#8 | protocol states and reply kinds are bare integers | open |
| peripherals#9 | a status getter mutates state | open |
| peripherals#10 | silent truncation of guest time in `i2s.advance` | open |
| peripherals#11 | undocumented fidelity shortcuts | open |
| peripherals#12 | GPIO-level parts assume a 32-pin `u32` level mask | open |
| peripherals#13 | the SPI SD card re-implements the SPI shifter | open |
| peripherals#14 | positional constructors with 20 to 40 arguments | open |
| peripherals#15 | dead or diagnostic-only state inside the models | open |
| peripherals#16 | `NEVER` defined three times, the literal three more | fixed in `d013662`: one `NEVER` in `sh.sysl.benchtop.timing`, imported by `v6m`, the peripherals and `virt`; the literals in `dsi` and `spi` are gone |
| peripherals#17 | "MIPI DCS" part carries ST7789 vendor commands | open |
| peripherals#18 | comments that name development steps, or are stale | open |
| peripherals#19 | `hd44780` pins are a `[7]int` indexed by position | open |
| peripherals#20 | sysl idiom: redundant suffixes, types and parentheses | open |
| peripherals#21 | 8-bit register pointers wrap through `+= 1` | fixed in `6252927`: FT6x06 (u8), GT911 and WM8994 (u16) wrap their auto-increment explicitly with a mask (sysl 0.7.8 has no `+%`); DS3231's pointer already wraps at 19 |
| peripherals#22 | tests assert implementation with magic numbers | open |
| ui-firmware#1 | example sources are not compiled by the gate | fixed in `80b8a2f` (every example firmware source is built in the suite) |
| ui-firmware#2 | the UI owns emulator control and the board's input model | open |
| ui-firmware#3 | duplicate, unreachable `"buzzer"` arm in `draw_part` | fixed in `61cac6f` |
| ui-firmware#4 | `draw_part` string switch, geometry duplicated in hit-testing | open |
| ui-firmware#5 | stringly typed session state; notices owned by prefixes | open |
| ui-firmware#6 | pot state stored twice and synced every frame | open |
| ui-firmware#7 | `rasterize` clones the desktop by hand-copying fields | open |
| ui-firmware#8 | a 20-argument positional constructor | open |
| ui-firmware#9 | hand-written SDL externs where the SDL binding owns the job | fixed in `d435c66` (sdl3 0.3.12: the touch window's size and scale through `sdl3.c`, `current_audio_driver()`, `current_time()`/`date_time()`; no `extern "SDL_` is left in benchtop) |
| ui-firmware#10 | instrument sources hard-code register maps | open |
| ui-firmware#11 | example firmware is not readable | open |
| ui-firmware#12 | startup and vector-table boilerplate copied into every example | open |
| ui-firmware#13 | redundant casts and suffixes in the examples and the UI | open |
| ui-firmware#14 | firmware state as dozens of prefixed module globals | open |
| ui-firmware#15 | clock and budget policy duplicated in a ternary chain | open |
| ui-firmware#16 | hand-rolled helpers the standard library has | open |
| ui-firmware#17 | dead code and unused imports | open |
| ui-firmware#18 | the TFT raster cache grows without bound | open |
| ui-firmware#19 | display renderers repeat the same scaffold | open |
| ui-firmware#20 | validation duplicated with different bounds | open |
| ui-firmware#21 | HOCON parse errors are discarded | open |
| ui-firmware#22 | tests in `ui/` that test firmware; hand-computed struct offsets | open |
| ui-firmware#23 | inconsistent formatting suggests no formatter | open |
| ui-firmware#24 | stale notes, development-step wording | partly fixed in `6f28a13` (the docs say what is true now); the source comments remain |

**Counts:** 88 findings — 8 fixed, 4 partly fixed, 76 open, 0 n.a.
