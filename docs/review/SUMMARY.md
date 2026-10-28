# benchtop `pico2-riscv` code-quality critique — summary

The branch is 196 commits and ~26k lines past dev. Four Opus reviewers each covered one area, read-only, on 2026-10-27/28. There are 88 findings in total; the full reports (each finding with file:line and the better shape) are beside this file:
- `cores.md` (21)
- `runtime.md` (21)
- `peripherals.md` (22)
- `ui-firmware.md` (24)

## Verdict
It works and its timing model is genuinely good. Structurally, though, it grew by accretion. Its central weaknesses all come from three missing abstractions:
1. **There is no machine/board object.** About 150 module-level globals hold the memory map, cores, devices and scheduler state. That means one board per process, sharing between threads that the compiler cannot check, and a ~84 MB static decoded-record table.
2. **There is no peripheral interface.** A closed `enum Device` is dispatched by hand in ~7 places, each model has its own read/write/advance signature, and a catch-all arm silently routes unknown devices to the RP block.
3. **There is no register-field modelling.** Raw masks are used everywhere, and the same field is decoded in several places. sysl's bitfields (`name: uW in u32`) are used nowhere.

## Top findings (severity order)
1. **Global machine state** (runtime #1, cores #2). See the verdict above. `assemble` (machine.sysl:72-274) is a 200-line function that writes globals before checks that can still fail, so a refused board description leaves the process half-configured.
2. **Locks that guard nothing** (runtime #4). `Mutex.new(0)` + hand-paired `lock_raw`/`unlock_raw` around 15 separate globals; `rp.sysl` locks only `if threads`, a flag that changes at run time. sysl's `Mutex[T]` with `with`/`Condvar` lets the lock own the data.
3. **Four schedulers over one set of globals** (runtime #3). A "Legacy" `pump_parallel` is still called from the UI, and the reset-pairing block is pasted twice.
4. **No peripheral trait** (runtime #2, peripherals #2). Adding a device touches ~7 places.
5. **The STM32H747 is pasted into 3 board files plus a 4th copy**, already drifting; there is no `mcus/stm32h747.hocon`, and pin routing is written twice per board (peripherals #3).
6. **The RISC-V core is not on any board.** It runs only in the flat-RAM `bench_riscv` (main.sysl:317), so a Pico 2 RISC-V image that touches a peripheral cannot run. It is also hard-wired to RP2350 facts, and pays about 100 loop iterations of interrupt/protection scans per instruction (cores #1).
7. **A new `Op` must be added in 5 places by hand.** Forgetting `configure_instruction_timing` silently makes it a 1-cycle op (cores #3). There are also about 400 lines of hand-unrolled `N -> &h[N, TIMED]` handler tables, which `for const` should replace (cores #4).
8. **The UI drives the emulator.** window.sysl runs the cores, instruments read guest RAM directly, and the UI imports `v6m` even for RISC-V/M7 boards. desktop.sysl:15's "UI state stays separate" is false. A duplicate `"buzzer"` arm (desktop.sysl:303 and :381) is dead (ui #2, #3).
9. **The examples are never compiled by the gate.** sysl.hocon:13 excludes them, yet ~20 tests embed their committed ELFs (~9 MB), so source and binary can drift silently (ui #1).
10. **Register writes are raw hex** in the peripherals and in the example firmware (`dsi_initialize`: eleven unnamed writes). One `main.sysl` produces 11 ELFs through `#if` blocks; startup code is copied into 4 files (peripherals #1, ui #5).

Smaller recurring patterns:
- run state held in strings (`"GUEST FAULT"`, prefix-matched notices);
- 8 or more hand-rolled ring buffers;
- hand-written `reset`s that already miss fields (sd_card);
- re-implemented `clz`/`popcount`/`rbit`/`sext`;
- `u32` flags where `bool` const parameters exist;
- timing tests that busy-spin on the wall clock;
- docs that contradict themselves ("currently/not yet" appears 13 times; stm32h747i-disco.md lines 72 and 116).

## What is good
- Timing is exact and does not depend on how finely it is advanced.
- Threaded dispatch with `become` and fused compare+branch is a justified speed trade.
- The parts → peripheral → adapter layering is clean where it exists.
- Board descriptions are validated data, not code.

## sysl findings (rule 9)
- **BUG, being fixed for 0.7.4** (branch `literal-ctx`):
  - `level = 1 << bit` (level: u32) → "cannot assign int to 'level' of type uint", and `level |= 1 << bit` → "'|' needs matching types". A literal on the left of a shift ignores the context.
  - `(if c then 0x15 else 5) | 7u32` is refused the same way.

  These forced some of the examples' `u32` suffixes.
- **Design question (user):** a closure captures by value (documented, expressions.md:1760), so `call(() -> hit = true)` silently writes the closure's copy and `hit` stays false. benchtop's SDMMC tests hit this. A pattern binding gets a diagnostic for exactly this ("a pattern binding is written once, because it holds a copy"); a write to a capture of a closure that is passed as a temporary, and so provably lost, could get one too. A 0.8.0 candidate.
- **Suspected, unprobed:** there is no fixed-capacity ring that lives inside a struct (`sysl.container.ring` takes outside storage), which is plausibly why every model hand-rolls one.
- **Not sysl's:** the user's sdl3 package lacks `SDL_GetWindowFromID` and `SDL_GetCurrentAudioDriver`, so benchtop declares them by hand. Add them to sdl3.
- `docs/sysl-wav-notes.md` is now stale: its positioned-I/O item shipped in 0.7.3, and its other items are covered by project_sysl_073 and the closure question above. It can be deleted.
