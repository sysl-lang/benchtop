# benchtop `pico2-riscv` — AREA = runtime (virt/, host/, top-level board/check/mcu/registry)

**Verdict.** The emulator works, but the runtime is a process-wide singleton: the board, both cores, every device and four schedulers live in ~150 module-level `var`s shared across files and host threads, held together by flags and comments instead of types.
Every new peripheral is copy-paste of one informal protocol (`configure_X`/`boot_X`/`signal_X`/`X_due`/`X_wake`/`X_read`/`X_write` + its own lock + its own globals), wired in by hand at five or more sites in `virt.sysl` and `machine.sysl`.
The threading is careful and mostly correct, but it can only be checked by reading the comments, because no lock owns the data it guards. This needs a structural pass before it becomes reading material.

## Findings

### 1. HIGH — No board object: the whole machine is module globals shared across threads
`virt/virt.sysl:147-224` (devices, `first`, `second`, `launched`, `starting`, `now`, `quantum`, `threads`, FIFOs, locks…), `virt/parallel.sysl:54-70,325-326` (`cores`, `clocks`, `dozing`, `wake_at`, `holding`, `floor`, `ended`, `slice_horizon`, `slice_paired`), `virt/workers.sysl:27-50` (21 `worker_*` globals), and one block of globals per device file (`sai_dma.sysl:12-43`, `storage.sysl:16-43`, `codec.sysl:11-32`, `adc.sysl:8-16`, `sound.sysl:20-25`, `dsi_worker.sysl:18-26`…).
- Why it matters: one board per process, so tests must re-`boot` to reset hidden state and can never run two boards. Ownership across threads is invisible to the compiler: a global bypasses the `&sync` capture checks the org rules depend on. `storage.sysl:46` reads the `storage_path` string from the disk thread, which the compiler would refuse as a closure capture. Who may write `now`, `launched` or `threads` from which thread is written only in comments.
- Better shape: put the state in a `Board` struct holding `[2]&Cpu`, a `Scheduler` and the devices, passed as `*Board`. Each device's state belongs in its own struct.

### 2. HIGH — Devices are a closed enum dispatched by hand, not a trait
`virt/virt.sysl:119-140` (`enum Device`), `:673-715` (`board_read`/`board_write` matches), `:556-557` (`device_due`/`device_wake` as hand-nested `min` chains), `:720-767` (`attach`), `:455-466` (twelve `boot_*` calls), `machine.sysl:242-252` (eleven `configure_*` calls), `machine.sysl:283-303` (`device_name`).
- Why it matters: adding a peripheral touches about seven places. Forgetting one fails silently. The `d -> rp_read(...)` catch-all at `virt.sysl:690/712` sends any new variant that has no arm to the RP block. A device missing from the `device_due` chain never gets its deadline honoured. The STM32 devices (SAI, DMA, DSI, RCC, SDMMC) and the RP2040 ones (SIO, Resets, Timer) share one flat enum and one module, so there is no chip-family boundary.
- Better shape: a `Peripheral` trait with `configure(b)`, `boot()`, `read`, `write`, `due()`, `wake(cpu)` and `signal(cpu)`. The bus holds `Buf[&dyn Peripheral]`, and `device_due` becomes a fold over that list.

### 3. HIGH — Four schedulers share one set of globals; mode flags branch inside one loop
`virt/virt.sysl:507-573` (serial `run_board`/`round`/`turn`), `virt/parallel.sysl:76-110` (`run_threads`, the "`benchtop run --threads`" path: **only tests call `set_threads`/`run_board` with threads now**), `parallel.sysl:332-369` (`pump_parallel`, which its own header calls "Legacy" yet `ui/window.sysl:45` still calls), and `virt/workers.sysl` (persistent workers).
- `run_core` (`parallel.sysl:115-152`), `doze`, `hold_back` and `turn` all branch on `worker_independent()`, `worker_checkpoint()`, `worker_clock_limit()` and `slice_horizon`. The lockstep lag-bound scheduler and the independently paced one are interleaved flag by flag.
- The "reset the pairing state" block is pasted twice: `parallel.sysl:345-356` and `workers.sysl:321-335`. The "`now` = later core clock" step appears three times, in two spellings: `workers.sysl:304,349` uses `max`, `parallel.sysl:367` uses a hand-written `if a > b then a else b` with `board_time` evaluated four times.
- `threads` means two things. It is the user's "run on threads" switch (`set_threads`, documented as "for the boards booted from now", although `boot` clears it at `virt.sysl:452`). It is also the live flag that the worker thread flips at runtime (`workers.sysl:291,308,335`).
- Better shape: one `Scheduler` trait (Serial, Lockstep, Paced) over a shared `Board`. Delete `run_threads`/`set_threads` and `pump_parallel` if the desktop no longer needs them. Split `threads` into a configuration value and a runtime state.

### 4. HIGH — Locks guard nothing: `Mutex.new(0)` + `lock_raw`/`unlock_raw` around separate globals
`workers.sysl:28` (`worker_gate = Mutex.new(0)`, guarding 15 separate `worker_*` vars), `parallel.sysl:54` (`gate`), `dsi_worker.sysl:21` (`dsi_done_gate`).
- Why it matters: `sysl.threads.Mutex[T]` exists to own its data (`with`, `@guards`). Here the guarded state is outside the lock, so the compiler cannot tell a locked access from an unlocked one. The code also pairs `lock_raw`/`unlock_raw` by hand around early returns (`workers.sysl:179-182`, `:374-376`).
- Holds that need no condvar use the raw pair anyway: `observe_core`, `observation_time`, `workers_observe`, `rebase_workers`, `submit_workers`, `wake_worker_checkpoints`. `Mutex.with` is the scoped form for those.
- Better shape: a `Mutex[WorkerControl]` that holds `paused`, `busy`, `closing`, `step_pending`, `requested`, `generation`, `finished` and `snap`. Use `with` for the short holds, and `lock_raw` only where `Condvar.wait` needs it, as `condvar.sysl:19` shows. This is not a sysl gap: the direct form exists and is unused.

### 5. HIGH — `assemble` is a 200-line god function that mutates globals before it can still fail
`virt/machine.sysl:72-274`.
- It mixes core validation, RAM-bank coalescing, flash/external-flash validation, peripheral placement, clock validation, eleven device configurations and per-core options.
- It writes `board_extra_ram`/`board_ram_*`/`board_flash_*` (`:211-215`) and calls `attach` (`:222`), and only then can return `Err` (`:220`, `:232`, `:239`, and every `configure_*?` at `:242-252`). A refused description therefore leaves the process half-configured with the new memory map. In the UI, which keeps running after a failed load, the next boot uses the mixed state.
- `attach` sets `core_kinds`/`core_priority_bits`/`core_dsp`/`core_fpu`, and `assemble:253-271` immediately overwrites them, so per-core configuration is written in two places.
- Better shape: `plan(b, reg) -> Result[BoardPlan, string]`, which is pure and does all the validation, followed by `apply(plan)`, which cannot fail. Each phase becomes its own function.

### 6. MEDIUM — Peripheral kinds and window sizes duplicated from the registry; one wrong error message
`machine.sysl:153-180` hard-codes each kind string → `Device` and window size (`0x400`, `256`, `0x1000`, `0x4000`…). The registry already carries `Window.size` (`registry.sysl:65`), and `mcu.sysl:320 window_size()` already computes it.
- `device_name` (`machine.sysl:283-303`) is the hand-written inverse, and it is wrong: `stm32-i2c` and `virt-i2c` both map to `I2c`, so a duplicate `stm32-i2c` is reported as "`virt-i2c`".
- The `stm32-dma` `controller` parameter is parsed twice, with different rules. `machine.sysl:156-162` reads anything other than 1 as `Dma2`. `sai_dma.sysl:68-74` computes `usize(n - 1)` with no bound, which traps on 0 or 3 if the registry's `PWhole(1,2)` check is ever bypassed.
- Better shape: one table of `(kind, Device)`, with sizes from `window_size(p, reg)` and names from the table. Parse `controller` once, into the `Mapped` entry.

### 7. MEDIUM — Magic register fields where the comments name the field
- `h7.sysl:9-12,29,37,52,55`: the comment names "GCR offset 0xa0, BOOT_C1 bit 2, BOOT_C2 bit 3", and the code uses `0xa0`, `4`, `8`, `12`.
- `sai_dma.sysl:193,291`: `(cr1 >> 10) & 3 == 1` (SYNCEN) appears twice. `:183` has `(b.slotr >> 16)` (SLOTEN). `:16` hard-codes DMAMUX request IDs `[87,88]`, and `:52/78` hard-code clocks `12288000`/`480000000`. `:149,276,287` hard-code the A/B block layout (`0x20`, `0x40`, `0x24`, stride 32).
- `dsi_worker.sysl:109` tests `packet.kind == 6 || 4 || 0x14 || 0x24` (MIPI read types).
- `storage.sysl:30-34` hard-codes default pins `5/7/6/4` and AF `5`.
- The SAI and DSI cases also break layering: `virt` reaches into `sai.Block`'s raw `cr1`/`slotr`/`frcr` fields. The SYNCEN test and the A/B register map belong in `peripheral/sai`, and `is_read(kind)` belongs in `peripheral/dsi`.

### 8. MEDIUM — Hand-rolled ring buffers, three times, when `sysl.container.ring.Ring` exists
`sai_dma.sysl:37-43,101-145` (output `[2][16384]` and input `[2][4096]`, each a separate head/count array pair), `codec.sysl:17-19,160-196` (`[4096]int`).
- Each one repeats `if count == N { head = (head+1)%N; count -= 1 }`, which is exactly `Ring.overwrite`. The capacities are bare literals repeated 4–6 times per ring.
- Better shape: `Ring[SaiPacket]`/`Ring[u32]` over static slots. The byte-copy loops (`storage.sysl:77,84,89`, `dsi_worker.sysl:88`) should be `sysl.slices.copy`.

### 9. MEDIUM — The DMA bus master is modelled as "whichever core last touched audio"
`sai_dma.sysl:24-27,154,160,218-221,224`.
- `audio_core` is a global set as a side effect in `bring_audio`. DMA reads and writes RAM through `memory.device_store(audio_cpu(), …)`, so the DMA's stores are attributed to a CPU's record-posting path. The IRQ is raised on "own" and poked on the other, depending on which core happened to advance audio.
- Device time is also driven by whichever core calls in. A lagging core's register access runs `bring_audio` at its own, earlier clock (`:237` only clamps `b.time` upward). The two cores therefore see a device whose "now" is the maximum of both callers' clocks.
- Better shape: give the DMA its own bus-master identity in `v6m` memory (a third requester that posts to both cores). Document or enforce how device time relates to a lagging core.

### 10. MEDIUM — Conditional locking keyed on a mutable global
`rp.sysl:57-61`: `hold_rp`/`free_rp` lock only `if threads`. The worker thread flips `threads` at runtime (`workers.sysl:291,308,335`).
- In the `!worker_parallel` worker mode, `threads` is false while a host worker runs the guest, so any off-thread access to RP state (UI snapshots) is unlocked.
- Every other device takes its `SleepLock` unconditionally (`sai_dma`, `storage`, `adc`, `h7`), and an uncontended futex lock is nearly free.
- Better shape: lock unconditionally.

### 11. MEDIUM — Host workflow reaches into virt globals and duplicates the image layout
`host/workflow.sysl:245-257`.
- `checked_image` reads `sh.sysl.benchtop.virt.board_flash_size`/`board_ram_at`/`board_flash_at` through fully qualified globals. It hard-codes `1048576usize` as the offset of flash inside the image buffer, a layout that `virt.sysl:391-408` defines implicitly as "RAM_BYTES of RAM, then flash". It also re-derives M-profile reset-vector rules (`pc & 1`, `pc < code_at + 9`), duplicating `boot`'s vector read (`virt.sysl:427-429`).
- Better shape: virt exports `reset_vector(image) -> Result[(sp, pc), string]` and an `image_code_offset()`. The host layer calls those.

### 12. MEDIUM — Per-core configuration as six parallel arrays, and copy-pasted core setup
`virt.sysl:193-198` (`core_instruction_timing`, `core_kinds`, `core_priority_bits`, `core_dsp`, `core_fpu`, plus `core_clock_divisors`), `virt.sysl:419-437` (core 0 and core 1 configured field by field, twice), `machine.sysl:270` (`fpu` as `2/1/0` from `"dp"/"sp"`).
- Better shape: `struct CoreConfig { kind, priority_bits, dsp, fpu: Fpu, timing, divisor }` stored as `[2]CoreConfig`, with an `apply(c, cfg)` used by both cores. `Fpu` should be an enum, not an integer code.

### 13. MEDIUM — Generic checker contains part-specific rules and duplicated defaults
`check.sysl` (added block in `check_parts`): a chain of `if part_model(p) == "sd-card" / "seven-segment" / "st7789" / "potentiometer" / "gt911" / "ds3231" / "hd44780"`.
- The fallbacks repeat the registry defaults: `240`, `320`, `3300000`, `93`, `2026`. `catalog_default` already exists in the same file.
- `PinUses.owner: Buf[(string, string, Span, string, bool)]` is accessed as `held.0`…`held.4` (`check.sysl:131-158`).
- Better shape: a `validate` hook on `PartSpec` (or in each `parts/` module), with defaults read from the spec. Use a named `PinOwner` struct.

### 14. MEDIUM — Boolean-trap positional constructors
- `host/workflow.sysl:66-67` builds `Workflow(...)` with 21 positional arguments, including `false, false, !c.program.is_empty(), c.program.is_empty() && c.watch`. Its state is the boolean soup `closed/building/build_requested/reload_requested/failed` plus a free-text `status` string, where a state enum belongs.
- `registry.sysl:196-437` `library()` is a 240-line function of `PeripheralSpec(..., true, specs(...), Window(0x400,false,0x400,8), "", names([]), false)` and `PartSpec("ft6x06", "...", true,false, ...)`. The `true`/`false` arguments are unreadable without the struct definition open.
- Better shape: named arguments or small builder functions, and an enum for the workflow phase.

### 15. MEDIUM — Wall-clock, busy-spinning tests
`virt/workers_tests.sysl:30-31,54,62,237,264,273,278,317-322` (and `worker_speed_tests.sysl`).
- The tests spin on `while monotonic() < until do ()` for 5–40 ms and assert wall-rate bounds: `retired > 1000` within 5 ms, `late_us > 5000`, and `time <= elapsed*2 + 2010`.
- Under `sysl test`'s parallel jobs on a loaded box (`~/CLAUDE.md` records a 14 s sleep that looked like a flaky test), these are flaky in one direction and burn a core each.
- Better shape: inject the host clock (`host_now_ns` is already one function), so pacing logic is tested on a fake clock. Keep one coarse real-time smoke test.

### 16. LOW — `start_workers` mutates state before it validates
`workers.sysl:86-99`. Ten `worker_*` globals are reset before the divisor check, which returns `Err`. Two validation styles are mixed: `require` for some preconditions, `Err` for one.
- Better shape: validate first, then mutate.

### 17. LOW — Asymmetric worker entry points
`workers.sysl:235` `cpu_worker0(ignored: *Cpu)` ignores its argument and calls `core0()`, while `cpu_worker1(cpu)` (`:273`) uses its own. `spawn(&cpu_worker0,&first,…)` passes a pointer that is then dropped.

### 18. LOW — Disk-queue backpressure becomes a guest-visible write error
`storage.sysl:85-88`. When the 64-slot host write queue is full, `sector_write` returns `false`, and the guest sees a failed SD write caused only by host I/O latency. `disk_failed` is also sticky across the session.
- Better shape: block, or apply backpressure through the card's busy signal, instead of failing.

### 19. LOW — Cache-line padding by hand, sized for the wrong machine
`virt.sysl:150-153`: `BusCache { index: usize, padding: [56]u8 }` pads to 64 bytes, but Apple-silicon L1/L2 lines are 128 bytes, so the two cores' entries still share a line. sysl has `@align(n)` (`reference/attributes.md`).
- Better shape: `@align(128) struct BusCache`.

### 20. LOW — sysl idiom
- Redundant suffixes, which I confirmed with a probe: `max(1u64, …)` and `if … then 1u64 else …` (`workers.sysl:228,249,252,260`) compile and infer `u64` without the suffix. `0usize … 1048576usize` (`workflow.sysl:248`).
- Hand-written `min`/`max` as `if a < b then a else b`: `sai_dma.sysl:207,229`, `parallel.sysl:291,367`, `machine.sysl:194`, `virt.sysl:246`.
- Fully qualified calls despite an alias in scope: `sh.sysl.benchtop.peripheral.sai.sample_bits` (`sai_dma.sysl:138`, with `sai` imported), `sh.sysl.benchtop.part_model` (`machine.sysl:142,148`).
- The 17-route IRQ loop is written three times (`sai_dma.sysl:215,245,262`).

### 21. LOW — Stale or misleading comments, dead paths
- `parallel.sysl:3` says "Legacy bounded batches", yet the UI calls the path.
- `parallel.sysl:6` describes `benchtop run --threads`, which no longer has a non-test caller.
- `machine.sysl:318` says "legacy pacing".
- `h7.sysl:7` says "Initial boot-control subset", a development-step phrasing.
- `machine.sysl:113-115` comments "A checked chip has `at`" and then silently defaults `at` to 0; it should `unwrap`, or at least assert.
- Errors hard-code "1 MB" (`machine.sysl:131,195,208`, `virt.sysl:407`) instead of deriving it from `RAM_BYTES`.

## sysl gaps suspected
- None confirmed. The `Mutex.new(0)` + `lock_raw` pattern looks like a workaround but is not one: `Mutex[T]` with `with` and `Condvar.wait` on a value-owning mutex already exists (`library/sysl/threads/mutex.sysl:96-126`, `condvar.sysl:19`).
- Probe run (`sysl 0.7.3`, scratchpad `bt-review/probe-runtime/main.sysl`): `max(1, hz / 20)` and `min(250000u64, if hz > 3 then 1 else hz - 1)` with `hz: u64` compile and print `2400 1`. The `1u64` suffixes are redundant, not load-bearing.
- Worth a look rather than a gap: module globals bypass the `&sync` crossing checks (finding 1). A disk thread reading a `string` global compiles where the same capture in a closure would be refused. This is by design, but the org's thread-safety story is weaker than the rules imply.

## What is good
- The threading is thought through. Lock-order comments (`workers.sysl:159-160`), the sleep/raise handshake (`parallel.sysl:239-241`) and the pause→busy→ack protocol are reasoned and mostly correct. I found no real data race on `now` and the batch state, because the `worker_busy` protocol serialises them.
- `wall_target`'s overflow is handled deliberately (`hz` is capped at 1 GHz, and the split division keeps every product under 2⁶⁴).
- `machine.sysl`'s refusal messages are specific and name the offending peripheral or core. The DSI worker and the disk thread keep host I/O off the guest threads, with bounded queues.
