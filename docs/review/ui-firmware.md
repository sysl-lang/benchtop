# benchtop review: ui-firmware (`sh/sysl/benchtop/ui/`, `examples/`, `docs/`)

**Verdict.** The UI works but has no real view/model boundary. `ui/` steps the emulator, maps parts to pins, reads raw guest RAM and binds SDL symbols by hand, and its state is stringly typed throughout (status, notices, held keys, pots).
The example firmware is not yet good reading material. One `main.sysl` builds 11 ELFs through `#if` soup. Register writes are bare hex addresses with no names, the startup code is copied into four files, and the shift-amount casts are redundant.
The examples also double as committed binary test fixtures (≈2.3 MB of ELFs plus 7 MB of RISC-V vectors). Their sources are excluded from `sysl test .`, so the source and the binaries the tests actually run can drift apart unseen.

---

## Findings (most severe first)

### 1. HIGH: Example sources are not compiled by the gate; tests run committed binaries built from them
`sysl.hocon:13` (`exclude = [... "examples/visual", "examples/matrix", "examples/embedded", "examples/audio", "examples/touch", "examples/sai-dma"]`). Tests embed the prebuilt binaries: `ui/window.sysl:14`, `ui/tests.sysl:12`, `ui/touch_tests.sysl:13`, `virt/sdmmc_tests.sysl:9-10`, `virt/codec_tests.sysl:8`, `virt/storage_tests.sysl:10`, `virt/recording_tests.sysl:12`, `riscv/hazard3_instruction_tests.sysl:5` and about 15 more.
- **What is wrong:** `sysl test .` never compiles the 2,000+ lines of example sysl. The binaries under test (`examples/touch/*.elf`, 11 images, ≈2 MB) are whatever was last built by hand.
- **Why it matters:** An example can stop compiling on the next sysl release, or diverge from its `.elf`, and nothing goes red. A reader who edits an example and re-runs the tests is testing the old binary.
- **Better shape:**
  - Either the gate rebuilds the fixtures (a build step, or a test that runs `build.sh --check` the way `generate-hazard3-tests.py --check` already does), or there is at least a test that builds each example and compares it with the committed ELF.
  - Move pure fixtures (`examples/riscv/*.bin`, the 5.3 MB `hazard3-instructions.bin`, `instructions.bin`) out of `examples/` into a `fixtures/` or `bench/` tree. They are test vectors, not examples.

### 2. HIGH: The UI owns emulator control and the board's input model (layering)
- `ui/window.sysl:23-84` (`restart`, `advance`, `advance_background`, `let_running`) is the run controller. It pumps cores, decides budgets and interprets `Stop` codes.
- `ui/scene.sysl:127-145` (`sample_inputs`) maps buttons, switches, pots and joysticks to GPIO pins and ADC voltages, then pokes `virt.panel_inputs` and `virt.set_analog_pin`.
- `ui/instruments.sysl:63-67` reads `cpu.ram[...]` with `atomic_load` directly, through `import sh.sysl.benchtop.v6m as cpu`.
- Why it matters:
  - Headless runs, tests and any future front end all have to go through `ui` to get correct input semantics.
  - The UI imports `v6m.{Cpu, Stop}` (`window.sysl:6`), so the front end is tied to one CPU module even for RISC-V and M7 boards.
  - The `Desktop` doc comment ("UI state stays separate from the guest", `desktop.sysl:15`) is not true.
- **Better shape:**
  - A `session` (or `virt.run`) module owns restart, advance and stop-to-status. It also owns "part input state → pin levels", exposed as `set_part_input(name, value)`.
  - The UI holds view state only and calls that API. Instruments ask `virt` for a `ram_peek(width, offset)` observation instead of reading the array themselves.

### 3. HIGH: Duplicate, unreachable `"buzzer"` arm in `draw_part`
`ui/desktop.sysl:303` and `ui/desktop.sysl:381`.
- **What is wrong:** The second `"buzzer" ->` arm (two fills, no label) can never run. Someone wrote a second rendering and never noticed the first one wins.
- **Why it matters:** It is dead code, and it is also a sign that sysl does not diagnose a duplicate string pattern in `match` (see gaps).
- **Better shape:** Delete one arm. More broadly, see finding 4.

### 4. MEDIUM: `draw_part` is a 90-line string switch over part models, with geometry duplicated in the hit-testing code
- **Where:**
  - `ui/desktop.sysl:300-388`.
  - The joystick knob geometry (`r.x + 20 + pos * (r.w - 40) / 10000`) at `desktop.sysl:378-380` is duplicated by the inverse mapping in `ui/touch.sysl:54-55`.
  - The pot ±0..10000 range and the 5000 centre value are repeated in `desktop.sysl:47`, `scene.sysl:157,166` and `touch.sysl:21`.
- **Why it matters:**
  - Adding a part kind means editing `draw_part`, `pressed`/`sample_inputs` and `pointer_touch`, plus the separate `part_model` match. The geometry duplicates will drift apart.
  - About 40 inline colour literals (`0x17232E`, `0x507386`, `0x0C1C29`…) sit beside the named palette in `scene.sysl:10-14`.
- **Better shape:**
  - A `PartRenderer` table (or a trait keyed by `part_model`) carrying `draw`, an optional `hit`/`drag` and the geometry helper shared by drawing and input.
  - Name the palette once (`SURFACE`, `BEZEL`, `KNOB`…).

### 5. MEDIUM: Stringly typed session state; notices are owned by matching prefixes
- **Where:**
  - `scene.sysl:30` sets `status: string`. `window.sysl:133-134,172` then compare `"GUEST FAULT"` and `"WAITING FOR BUILD"`.
  - `window.sysl:308,315` decides whose notice it is with `starts_with("Emulation speed:")`, `== "Firmware reloaded"` and `starts_with("Saved ")`.
  - Held keys are stored as `"key:part"` tokens and re-split (`scene.sysl:106,124`).
  - Pots and switches are linear `Buf[(string, int)]` association lists (`scene.sysl:37-38,152-178`).
  - Touch capture mixes `"joystick:<name>"` with bare element names in one string (`touch.sysl:50,73`).
- **Why it matters:** A typo or a reworded message silently changes behaviour, and the compiler cannot help.
- **Better shape:**
  - `enum RunStatus { Running, Paused, Waiting, Breakpoint, Exit(int), Fault, WaitingForBuild, Preview }` with a `label()`.
  - `enum NoticeKind` plus the text.
  - `Held(key, part)` as a struct.
  - `enum Capture { None, Joystick(string), Glass(string) }`.

### 6. MEDIUM: Pot state is stored twice and synced every frame; copy-modify-set on a Signal everywhere
- **Where:**
  - `Desktop.pots: Buf[PotControl]`, each holding a `Signal[int]` (`desktop.sysl:33,38-40`), and `State.pots` (`scene.sysl:37`).
  - `sync_pots` copies one into the other on every tick (`desktop.sysl:51-52`, `window.sysl:198`). The knob drag writes both (`desktop.sysl:273-275`).
  - The `var st = d.current.read(); …; d.current.set(st)` pattern appears about 15 times (`desktop.sysl:56-60,156-158,242-260`, `window.sysl:147-149,188-192,197-216,223-245`, `instrument_window.sysl:45-47`, `touch.sysl:37-39`).
- **Why it matters:**
  - Two sources of truth.
  - `BoardView.paint` mutates model state and calls `d.current.set` from inside **paint** (`desktop.sysl:243-275`), so rendering has side effects. `rasterize` has to work around that by building a throwaway desktop.
  - `desktop_tick` sets `st` at line 209, runs `instrument_tick` and `save_desktop`, then sets the same stale `st` again at line 216: a lost-update pattern waiting to bite.
- **Better shape:** One owner per datum (the signal, or the session model). Handle input in event handlers, not in `paint`, and give `State` small mutators that do read-modify-set in one call.

### 7. MEDIUM: `rasterize` clones the desktop by hand-copying fields
`desktop.sysl:406-415`.
- **What is wrong:** It builds a fresh `Desktop` and then copies seven fields one by one. Joystick axis signals are reset to 5000 (`desktop.sysl:47`), so a PNG saved mid-drag shows the joystick centred. It also writes `board_rect` back into the live desktop as a side effect.
- **Why it matters:** Every new `Desktop` field has to be remembered here as well, and the joystick case already shows one that was missed.
- **Better shape:** Separate render inputs (a `DesktopView` snapshot) from interactive state, and give `Workbench` an `interactive` flag that it honours. Or add `Desktop.snapshot()` next to the struct definition.

### 8. MEDIUM: A 20-argument positional constructor, and long positional-boolean calls
- **Where:**
  - `desktop.sysl:48-49`: `Desktop(s, ui.signal(st), ui.signal(false) ×4, ui.signal(""), …, false, false, interactive, None, …)`.
  - `scene.sysl:40`: `State(…12 positional…)`.
  - `window.sysl:304`: `launch(which, "", false, 1, screenshot, true, instrument_file = …)`. `launch` takes 9 parameters (`window.sysl:87`).
- **Why it matters:** Reordering two `bool` or `Signal[bool]` fields compiles and is wrong.
- **Better shape:** Named arguments at construction (or field defaults), and a `LaunchOptions` struct.

### 9. MEDIUM: Hand-written SDL externs in benchtop where the SDL binding owns the job
- **Where:**
  - `ui/touch.sysl:11-14` declares `SDL_GetWindowFromID`, `SDL_GetWindowSize`, `SDL_GetWindowSizeInPixels` and `SDL_GetWindowDisplayScale` as raw `*u8` externs.
  - `ui/audio.sysl:15` declares `SDL_GetCurrentAudioDriver`.
- **What is wrong:** `instrument_window.sysl:53,83-85` already uses the binding's `window.size_in_pixels()`, `size()` and `display_scale()`. Only "window from id" and "current audio driver" are missing from `sysl-lang/sdl3`, and nothing else in `~/dev/sysl-lang/sdl3*` binds either symbol.
- **Why it matters:** This is a rule-9 workaround: an untyped `*u8` window handle in application code. The scale computation is also duplicated (`touch.sysl:92-94` vs `instrument_window.sysl:83-86`).
- **Better shape:** Add `sdl.window_from_id(id) -> Option[&Window]` (borrowed) and `sdl.current_audio_driver()` to sdl3, then share one `event_point(window, e)` helper.

### 10. MEDIUM: Instrument sources hard-code register maps and assume a 16-register ARM core
- **Where:**
  - `ui/instruments.sysl:31-41` keeps its own copy of the virt-ADC register offset table (0, 4, … 32) in the UI. Nothing else in `sh/sysl/benchtop` names `CONVERSION_CYCLES`, so the peripheral and the UI can drift.
  - `instruments.sysl:49-56` hard-codes `0..<2` cores and `r0..r15`, which is meaningless on RV32 (x0..x31) and on boards with a different core count.
  - `cpu_rates.sysl:12-36` hard-codes two cores as well.
- **Better shape:** The peripheral exposes a named register list (`adc.register_offset(name)`), and the core exposes `register_names()` and `core_count()`.
- **Docs:** `docs/instruments.md:43` says "the windowed Cortex-M backend currently binds…", which is honest, but make it explicit that RISC-V boards get the wrong names.

### 11. MEDIUM: Example firmware is not readable: anonymous register writes and `#if` soup
- **Where:**
  - `examples/touch/main.sysl:198-215` (`dsi_initialize`) is eleven `word(0x500000a0).value = 3`-style writes with no register or field names.
  - `dsi_window` (`:170-179`) and `dsi_short` (`:165-168`) work the same way.
  - `storage_spi.sysl:62,82,100-102` writes `word(0x58020000)` (GPIO MODER) with masks like `~0xff00`.
  - `audio/main.sysl:56` (`audio.ctrl = 3 // TX and RX enabled`) and `visual/main.sysl:384` rely on comments in place of names.
  - `touch/main.sysl` alone has `#if` blocks across `feature_audio`, `h7_cores`, `dsi`, `microphone`, `delay`, `storage`, `sdmmc`, `gpio_spi`, `joystick` and `full_redraw`, producing 11 ELFs (`touch/build.sh:5-16`).
  - `main.sysl:31-58` builds the vector table through nested `#if`s, interleaving fields with preprocessor lines.
- **Why it matters:** The examples are the user's showcase ("examples show the pleasant factor"). A reader learns that sysl firmware is magic numbers and conditional compilation.
- **Better shape:**
  - Register structs with named fields, as `Spi`, `Audio` and `Gpio` already do for the virtual rig, and `const` field masks.
  - Split the Discovery firmware (DSI, audio, storage) from the virtual-rig touch demo, with one entry file per board.
  - Feature flags only for the additive pieces (microphone, delay, storage).

### 12. MEDIUM: Startup and vector-table boilerplate copied into every example
`initialize_memory`, `StartupByte` and the `__data_*`/`__bss_*` externs are repeated in `examples/audio/main.sysl:3-20`, `matrix/main.sysl`, `touch/main.sysl:6-24` and `visual/main.sysl`. `Vectors` with a hand-counted `reserved: [46]u32` is repeated too, along with `AudioVectors` using `[71]u32`/`[2]u32`/`[179]u32` slot counts (`touch/main.sysl:32-42`).
- **Why it matters:**
  - Four copies to keep in step.
  - The hand-counted slot gaps are an off-by-one waiting to happen: an IRQ in the wrong slot fails silently.
- **Better shape:**
  - A tiny shared `examples/runtime` module (touch already has `runtime.sysl`).
  - A vector table written as `[N]Handler` indexed by named IRQ constants (`vectors[16 + DMA2_STREAM1_IRQ] = &audio_interrupt`). Whether sysl can fold that at module level should be probed (see gaps).

### 13. MEDIUM: Redundant casts and suffixes in the examples and the UI (probed)
- **Probe:** sysl 0.7.3 compiles `level |= 1u32 << bit` with `bit: int`, so every `u32(...)` on a **shift amount** is redundant.
- **Locations of the redundant shift-amount casts:**
  - `examples/audio/main.sysl:75` (`16384u32 << u32(i * 2)`)
  - `matrix/main.sysl:56-57`
  - `visual/main.sysl:209,231`
  - `touch/main.sysl:245`
  - `ui/desktop.sysl:458` (`1u8 << u32(bit)`)
  - `ui/lcd.sysl:31`
  - `ui/scene.sysl:91-92,136`
  - `instruments.sysl:66`
- **Other redundant type annotations:**
  - `var phase: int = 0` (`audio/main.sysl:57`)
  - `var delay_ui_feedback: int = 35` and `delay_ui_wet` (`touch/delay.sysl:15-16`)
  - `var ww: int = 0` ×4 (`ui/touch.sysl:88-91`)
  - `var start: usize = output.len` (`desktop.sysl:118`)
  - `val transcript: Buf[u8] = … buf()` and `val output: []const u8` (`desktop.sysl:113-114`)
- **Array-literal suffixes:** `for shift in [0u32, 8u32, 16u32]` (`desktop.sysl:475`) should annotate the binding.
- Several remaining suffixes are forced by an inference gap (see gaps), so they are not the examples' fault.

### 14. MEDIUM: Firmware state as dozens of prefixed module globals
- **Where:** `examples/touch/audio.sysl:12-36` (`audio_phase`, `audio_envelope`, `audio_target`, `audio_step`, `audio_effect`, `audio_filter`, `audio_peak`…) and `touch/delay.sysl:9-19` (11 `delay_*` globals).
- **What is wrong:**
  - The `audio_`/`delay_`/`play_` prefixes are namespaces done by hand.
  - `audio_steps: [10]u32 = [39370534, …]` (`audio.sysl:75`) are phase increments computed offline with no formula shown.
  - The 256-entry sine table (`audio.sysl:40-73`) and the 100-entry one (`audio/main.sysl:48`) are pasted literals.
- **Better shape:**
  - A `struct Synth`/`struct Delay` held in one global, with methods.
  - Phase steps written as `step(note_hz) = u32(note_hz * 2^32 / 48000)` (folded).
  - Tables computed by a folded loop if `sin` folds; otherwise the comment should say why the literal is there.

### 15. MEDIUM: Clock and budget policy duplicated in a ternary chain with magic rates
- **Where:** `ui/window.sysl:151` and `ui/window.sysl:163` compute "which clock drives pacing" twice, with the literals `12000000`, `1200000` and `20000`. Line 163 is a 5-way `if … elif … else if …` (mixing `elif` and `else if`) inside a call argument.
- **Better shape:** One `pacing_clock(d) -> PacingSource` function with named constants. The budget is then a `match` on it.

### 16. LOW: Hand-rolled helpers that the standard library already has
- `desktop.sysl:423` redefines `PI` while `sysl.math.pi` exists (`library/sysl/math/constants.sysl:17`).
- `ui/audio.sysl:28` scans `vars()` for `BENCHTOP_AUDIO_TRACE`, while `cpu_rates.sysl:40` correctly uses `sysl.env.get(...) == Some("1")`.
- `window.sysl:268-271` lowercases through `sysl.raw.str_cast` over a hand-built `Buf[u8]`. That is an unsafe import in UI code for an ASCII lowercase that `sysl.text` provides.
- `desktop.sysl:132-141` wraps build-log lines by appending one character at a time (`chunk += s"$ch"`), which is quadratic and uses the magic width 36.
- `desktop.sysl:437` spells the call fully qualified as `sh.sysl.benchtop.part_whole` despite the imports.

### 17. LOW: Dead code and unused imports
- `ui/audio.sysl:5` (`import sh.sysl.benchtop.v6m.clock`) is unused.
- `board_transform(s, r)` (`desktop.sysl:201-202`) ignores `s` and always returns scale 1.0, so every caller passes a scene for nothing. The comment on `Transform`/`fit` (`scene.sysl:60`) describes letterboxing that `board_transform` no longer does.
- `ui/audio.sysl:12-13`: `audio_trace` and `audio_trace_frame` are mutable module globals used only for a debug print.

### 18. LOW: The TFT raster cache grows without bound and is searched linearly
`ui/tft.sysl:31-41`. Entries are keyed by name and size and never evicted. A display that changes resolution, or a window-density change, pushes a new `TftRaster` each time while the old ones are kept. The cache is a `Buf` that lives on `Desktop` and is copied into the read-only desktop as well.
- **Better shape:** Replace in place on a size change, and key by name.

### 19. LOW: The display renderers repeat the same bezel, screen and label scaffold
`ui/tft.sysl:24-27,54`, `ui/oled.sysl:10-18,38` and `ui/lcd.sysl:10-13,33`, plus the `"i2s-amplifier"` and `"ds3231"` arms in `desktop.sysl:316-325`, which are identical except for the label.
- **Better shape:** A `draw_module(c, r, bezel, label)` helper; collapse the two identical arms into one with a per-model caption.

### 20. LOW: Validation is duplicated with different bounds
`instruments.sysl:119` validates `capacity` as 1..65536, but `instrument/plot.sysl:33` requires 2..65536. Capacity 1 therefore produces the plot's message rather than the field's. `docs/instruments.md:73` says 2–65536.
- **Better shape:** One bound, owned by `plot`.

### 21. LOW: HOCON parse errors are discarded
`instruments.sysl:94-96` (`Err(_) -> "invalid instruments HOCON"`) drops the location and reason. The unknown-field check (`:111`) is a 10-term `&&` chain.
- **Better shape:** `Err(e) -> Err(e.render(src))`, and `KNOWN_FIELDS.contains(f.key)`.

### 22. LOW: Tests in `ui/` that test firmware, and asserts on hand-computed guest struct offsets
- `ui/touch_tests.sysl:29-61` (`firmware_ui`) exercises the virt peripherals and the guest, but not the UI (apart from `release_touch`).
- The test reads `state + 4`/`state + 8` assuming the guest `AppState` layout (`examples/touch/main.sysl:106-109`), and asserts one exact pixel colour, `0xE7EBEF` at `125*240+180` (`:52`).
- **Why it matters:** Reordering a field in the example breaks the test with a misleading message.
- **Better shape:**
  - Export each observed field as its own symbol (as `audio_errors` and the others already are).
  - Assert on a region or a colour class, not one pixel.
  - Move firmware-behaviour tests next to `virt`.

### 23. LOW: Inconsistent formatting suggests no formatter
Comma spacing is mixed even within one function: `PotControl(s"${p.name}.$axis",ui.signal(5000))` at `desktop.sysl:47`, the `"switch"`/`"joystick"` arms at `:357-380` against the rest of `draw_part`, all of `touch.sysl` and `tft.sysl`, `[0.0;4096]` at `audio.sysl:69`, and `advance` at `window.sysl:45-52`. Two blank lines appear after `sync_pots` (`desktop.sysl:53-54`).

### 24. LOW (docs): Stale notes, and development-step wording that will rot

**`docs/sysl-wav-notes.md` is stale and in the wrong place:**
- It is a sysl-friction report ("Using installed Sysl 0.7.0"), not benchtop documentation.
- Item 1 (positioned I/O) is now `library/sysl/io/positioned.sysl` on sysl dev, delivered as 0.7.3 from these very notes.
- Item 2 (Option payload mutation) was taken up the same way.
- Items 3 and 4 are real sysl findings that belong in a message to the `sysl` session, not in this repo: an overlapping mutable view accepted silently, and a mutable scalar capture whose writes are silently lost.
- Recommendation: delete the file after forwarding items 3 and 4.

**`docs/stm32h747i-disco.md` contradicts itself:**
- Line 72 says "M7 currently spins and is reserved for subsequent audio firmware; it does not yet process audio".
- Line 116 says `audio-disco.elf` "runs sine DSP on M7".
- The first is true only of the `h7` build, and the wording is a development-step note that has already gone stale.
- The same wording ("currently", "not yet", "future work", "remains to be implemented") appears 13 times across `docs/*.md` and `examples/*/README.md`.
- Recommendation: state what each image does, and put the unsupported features in a "Not modelled" list.

**Claims checked and accurate:**
- DSI at 0x50000000 and I2C4 at 0x58001c00 with FT6206 at 0x2a (`boards/stm32h747-disco.hocon:21,31`).
- The 1024-byte DSI packet cap (`virt/dsi_worker.sysl:16`).
- The instruments source table matches `instruments.sysl`.
- The README's `sysl test . --filter RV32` (`--filter` exists).

**`examples/riscv/` is a fixture directory with a README, not an example.** See finding 1.

---

## sysl gaps suspected

1. **A literal on the left of `<<` does not take the expected type from context.** Probed on sysl 0.7.3.
   - Probe:
     ```
     var level: u32 = 0
     level |= 1 << bit
     ```
   - Result: `error: '|' needs matching types, got uint and int` at `1 << bit`.
   - `1 << pins[i]` with a `u32` count is accepted, and so is `1u32 << bit` with an `int` count.
   - The context (`u32 |=`) should type the literal, so the examples' `1u32 << …` suffixes are forced by this gap. They are not redundant.
2. **An `if` whose arms are literals does not unify with the other operand.** Probed: `(if has then 0x15 else 5) | 7u32` fails with `'|' needs matching types, got int and uint`. This forces `0x15u32 else 5u32` at `examples/touch/main.sysl:167` and `if … then 1024u32 else room` at `playback.sysl:48` and `recording.sysl:120`.
3. **A duplicate string pattern in `match` is not diagnosed** (`desktop.sysl:303` and `:381`, both `"buzzer"`). Not probed in isolation, but the branch compiles with both arms present.
4. **A per-target `asm` block must list every other architecture as `unavailable`.** `examples/touch/main.sysl:80-88` lists `[aarch64, x86_64, riscv64, riscv32, wasm32, craft] unavailable "Cortex-M firmware"`, twice. A firmware file that only targets thumb should be able to say so once, through a module-level target requirement or a `wfi()`/`nop()` intrinsic in a `sysl.arm` module. Not probed.
5. **From `docs/sysl-wav-notes.md`, unverified here:**
   - An overlapping mutable view accepted silently (FAT cache passed as its own read output).
   - A mutable scalar captured by a closure, written in the callback, with the write silently lost (0.7.2).

   Either is worth a reduction from the `sysl` session. The second, if confirmed, is a diagnostic that should exist.
6. **Missing sdl3 binding surface** (`SDL_GetWindowFromID`, `SDL_GetCurrentAudioDriver`). This is the user's own package, so it falls under rule 9 (finding 9).
7. **Probe candidate, not run:** whether a module-level `val` vector table can be built by indexing (`var t = [0; N]; t[16 + IRQ] = &handler`) and still fold. That would remove the hand-counted `[71]u32` gaps.

## What is good

- The instrument plot model (`sh/sysl/benchtop/instrument`) is cleanly separated from sources. `Source` is a closure returning `Option[real]`, and observations are documented as side-effect-free. `docs/instruments.md` describes the behaviour honestly, limits included.
- The RISC-V fixtures come with real oracles: LLVM-assembled `compressed.S`, host-compiled `kernel.c` and a `--check` mode on the generator. That is the right instinct, even if the outputs live in the wrong directory.
- The UI tests do exercise real firmware end to end (GPIO → pixels, touch → guest counter), and the docs are specific about what is *not* modelled, which is rare.
