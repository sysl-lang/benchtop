# benchtop review — AREA: peripherals (peripheral/, parts/, boards/, mcus/)

**Verdict.** The behaviour is careful: timed, rational-clock models, bounded storage, and refusing modes that are not implemented rather than faking them. But the code is written as raw bit arithmetic. Registers are numeric offsets in `match` arms, fields are inline shifts and masks, and states are bare integers, even though sysl has C bitfields and unions for exactly this.
There is no shared peripheral shape: no trait or common register-file abstraction. There are two ownership conventions (`*T` value types and `&T` reference types), three ways to write `reset`, and five hand-rolled ring buffers.
The board format is sound data (HOCON, validated against `registry.sysl`), but the STM32H747 has no `mcus/` file and is pasted inline into three boards, with pin routing written twice in each.

## Findings

### 1. HIGH — registers are magic offsets and masks everywhere; sysl's bitfields/unions are never used
- `peripheral/sdmmc/sdmmc.sysl:226-283` (offsets `0,4,8,…,92`; `~0x000353ff`, `~0x13ff`, `~0x20f3`, `~0x18200dff`, `~0x1fe0`)
- `sdmmc.sysl:94-131`: status bits written as `4`, `64`, `128`, `1`, `1 << 11`, `1024`, `256`, plus the card-status error mask `0xfdffe008`.
- `peripheral/dsi/dsi.sysl:55-153` (`h.regs[0x430 / 4] & 0x01000001`, `0xb0 → 0x15 | 0x80`, `0x40c`, packet kinds `0x37`, `0x29`, `0x39` …)
- `peripheral/sai/sai.sysl:55-104`: `(cr1 >> 5) & 7`, `1 << 19`, `1 << 26`, `(b.slotr >> 6) & 3`, `(3 << 2) | (1 << 12)`.
- `peripheral/dma/dma.sysl:48-140`: `cr & 4/8/16`, `(cr >> 11) & 3`, `1 << 9`, `1 << 10`, `1 << 15`, `0x3d`, `0x83 | 0x20`.
- `peripheral/stm32_spi/stm32_spi.sysl:44-62`: `0x1201`, `0x04400000`, `0x70000007`, `0x87400000`, `1 << 25`.
- `parts/wm8994/wm8994.sysl:24-192`: register numbers `0x26`, `0x612`, `0x406`, … and power masks `0x404`, `0x808`, `0x202`, `0xe0` and so on, with no names.
- `parts/ft6x06`, `parts/gt911` and `parts/ds3231` (`registers[14]`, `registers[15]`) follow the same pattern.

**Why it matters.** None of this can be checked against the reference manual without the manual open beside it. The same field is decoded several times with no shared definition: the SDMMC bus width `h.clock & 0xc000 == 0x4000` appears at `:84`, `:122` and `:255`, and the DSI "read packet kind" predicate is spelled out twice (`:46` and `:68`). A typo in one mask fails silently.

**Better shape.** Use one named constant per register offset (`const SDMMC_CMDR = 0x0c`), and declare each register's fields with sysl's own bitfield syntax, e.g.
```
struct Clkcr
    clkdiv: u10 in u32
    _: u2 in u32
    pwrsav: bool in u32
    …
```
wrapped in a `union` with a `u32` word (types.md §Unions: "a register read as a word or as its bits"). Then `clock.widbus == 1` replaces `clock & 0xc000 == 0x4000`. The language already offers the direct form, so this is not a gap. `grep " in u32"` across the whole of `sh/` returns nothing.

### 2. HIGH — no common peripheral interface; every model has its own `read`/`write`/`advance` signature
- `sdmmc.read(h, offset, card, time)` and `sdmmc.write(h, offset, value, time)`
- `sai.read(b, offset)` and `sai.advance(b, time, endpoint)`
- `dsi.advance(h, time, endpoint)`
- `stm32_spi.read(c, offset, width)` and `stm32_spi.write(c, offset, width, value)`
- `adc.write(a, offset, value, uv, connected, time)`: the sampled analogue input is passed into every register write (`peripheral/adc/adc.sysl:51`).
- `i2s.advance(c, time)` (no endpoint); `pwm.read(p, offset, time)`.
- `due()` returns `u64` with a `NEVER` sentinel in sdmmc, `Option[u64]` in `peripheral/spi/spi.sysl:104`, and is a bare field in sai and dsi.

**Why it matters.** `virt/machine.sysl:153-168` has to special-case each kind by string (`"stm32-sai" -> placed(Sai,…)`). Each new peripheral then needs bespoke glue in the adapter, which is the copy-paste the brief warns about. Time, the bus-width check and "no card attached" (sdmmc takes both `c: Option[&Native]` and `routed: bool` for the same purpose, `sdmmc.sysl:136`) are threaded through by hand.

**Better shape.** Define a `trait Mmio { read(offset, width, time) -> Option[u32]; write(offset, width, value, time) -> bool; advance(time); due() -> Option[u64]; irq() -> bool; reset() }`. Each peripheral's external connections (card, endpoint, RAM callbacks, analogue source) are then set once at construction instead of being passed on every call.

### 3. HIGH — the STM32H747 MCU is pasted inline into three board files; there is no `mcus/stm32h747.hocon`
- `boards/stm32h747-disco.hocon:4-26`, `boards/stm32h747-sd.hocon:4-31` and `boards/stm32h747-spi-sd.hocon` (the diff against `-sd` is about 10 lines).
- `boards/h747-cores.hocon` defines a fourth copy, under the name `controller`.

`rp2040` and `rp2350` have proper `mcus/` files, referenced as `mcu = rp2350`. The H7 boards repeat the core list, clocks, RAM and flash map, RCC, SAI, DMA2, DMAMUX, DSI and I2C4 by value.

**Why it matters.** Changing one base address, IRQ number or clock means editing three files, and the three copies have already drifted: only `-spi-sd` gives `gpioa` its `reset-mode`. The MCU name `STM32H747XIH6` is also hard-coded into part wiring (`detect = STM32H747XIH6.PI8`).

**Better shape.** Add `mcus/stm32h747.hocon` with the full peripheral map, including every GPIO bank and its datasheet reset `MODER`, and have each board say `mcu = stm32h747` plus its parts.

### 4. MEDIUM — MCU facts live in board files, and pin routing is written twice per board
- `boards/stm32h747-spi-sd.hocon`: `gpioa { … reset-mode = 0xabffffff }`. The GPIOA reset value is a silicon fact, not a board choice. GPIOB's (`0xFFFFFEBF`) is not modelled anywhere, and `stm32_gpio.bank()` defaults every bank to `0xffffffff` (`peripheral/stm32_gpio/stm32_gpio.sysl:15`).
- `boards/stm32h747-sd.hocon:20` and `:39`: `sdmmc1 { signals { ck = [PC12] … } }` repeats the same pins in `buses { sdmmc1 { ck = PC12 … } }`. `i2c4` is likewise written twice in all three H7 boards.

**Better shape.** The MCU file lists each signal's possible pins; the board picks one in exactly one place.

### 5. MEDIUM — the board/MCU schema is inconsistent with itself
- Key spelling is mixed: `mpu_regions` (`mcus/rp2350.hocon:12`) beside `clock-hz`, `reset-mode`, `kernel-clock-hz` and `instruction-timing`.
- Part wiring uses two syntaxes: `wire { anode = GP25 }` (`boards/pico2.hocon:8`) and `anode = controller.GP0` at the top level (`boards/workbench.hocon:21`).
- Pin qualification is mixed: bare `GP25`, `controller.GP0` and `STM32H747XIH6.PI8`.
- The Hazard3 MCU description is in `examples/riscv/hazard3.hocon` instead of `mcus/`, and `mcus/rp2350.hocon:5-6` still says "Hazard3 RISC-V cores are not described yet … does not run it yet", which is stale on a branch whose commit `01a0965` enables Hazard3.

**Better shape.** Choose one key convention (kebab-case, as the majority already uses), one wiring form and one pin-reference form, and have `registry.sysl` reject the others.

### 6. MEDIUM — two ownership conventions and three `reset` styles across peripherals of the same kind
- Value type plus `*T` methods: `peripheral/spi`, `stm32_spi`, `i2c`, `i2s`, `pwm`, `adc`, `stm32_gpio`, `parts/spi`, `hd44780`, `ssd1306`, `seven_segment`.
- `&T` reference type: `sdmmc`, `sai`, `dsi`, `dma`, `sd_card`, `wm8994`, `ds3231`, `ft6x06`, `gt911`, `mipi_dcs`.
- The three `reset` styles:
  - Rebuild from the constructor: `*c = controller(c.capacity)` (spi, i2s, pwm, adc, hd44780).
  - Allocate a whole new heap object only to copy it: `*h = *host(h.hz,h.kernel)` (`sdmmc.sysl:61`).
  - Hand-list every field, which is error-prone:
    - `sai.sysl:38-53` repeats every constructor default.
    - `wm8994.sysl:19-52`: `codec()` builds every field and then calls `reset`, which overwrites them all again. `regs[0x100] = 0` immediately after a zeroing loop is a no-op.
    - `sd_card.sysl:42-60` forgets `data_count`, `write_sector` and `data`.

**Better shape.** Use one convention: stateful device models as `&T`. "Reset" should mean "rebuild register state from one `defaults()` function, keeping the configuration fields", written once per model.

### 7. MEDIUM — five or more hand-rolled ring buffers duplicate the same index arithmetic
- `sai.sysl:15-30, 133-143` (`fifo[(head+count)%8]`)
- `i2s.sysl:19-30, 42-82` (four rings: tx, rx, input and output, each with head/count fields and `% FIFO` / `% HISTORY`)
- `peripheral/spi/spi.sysl:12-17` (`& (capacity-1)`)
- `gt911.sysl:44-56` (`% 8`)
- `sd_card` reply cursor; `dsi` rx cursor

**Why it matters.** `sysl.container.ring` already exists. Each copy re-derives wrap and full/empty logic: i2s even hard-codes `[0;64]` beside `const FIFO = 64`, and the wrap is a mask in one copy and a modulo in another.

**Better shape.** Use a fixed-capacity inline ring type. See "sysl gaps suspected": `Ring` takes external storage, which may be why nobody used it.

### 8. MEDIUM — protocol states and reply kinds are bare integers
- `parts/sd_card/native.sysl:8` `state: u32`: 0 to 7 are idle, ready, ident, stby, tran, data, rcv and prg, compared as `n.state == 4` and `n.state = 7` throughout `:28-131`.
- `Reply.kind` (0 to 3) and `pending` (a command index used as a mode).
- SPI `write_stage` 0/1/2 (`sd_card.sysl:189-208`).
- `wm8994.stage` 0 to 3 (`:83-99`).
- `sdmmc` checks `reply.kind == 2 || reply.kind == 3` (`sdmmc.sysl:105`).

**Better shape.** A bare `enum State { Idle, Ready, Ident, Stby, Tran, Data, Rcv, Prg }` and an `enum ResponseKind`. sysl gives a bare enum `==` for free.

### 9. MEDIUM — a status getter mutates state
`parts/sd_card/native.sysl:27-28`: `status()` moves `prg → tran` as a side effect. `native_busy` (`:129-130`) calls `status(n,time)` only for that side effect and discards the result. Any read of status therefore changes the card.

**Better shape.** Add an explicit `settle(n, time)` that performs the transition, and call it from `native_command` and `native_busy`. `status` then stays pure.

### 10. MEDIUM — silent truncation of guest time in `i2s.advance`
`peripheral/i2s/i2s.sysl:55`: `count = min(ticks / c.hz, HISTORY)`. After a gap longer than about 85 ms at 48 kHz, the extra sample periods are dropped. The guest TX FIFO is drained and the underrun flag set only for the first 4096 frames, so guest-visible timing depends on how often the host calls `advance`. This contradicts the comment at `:50`, "independent of UI frame size".

Related: `c.peak` (`:78`) is the absolute value of the *last* sample, not a peak.

**Better shape.** Advance the guest-side FIFO state for every period. Bound only the host-side history ring.

### 11. MEDIUM — undocumented fidelity shortcuts
- **Strict refusals.** Writing a reserved or unsupported value returns `false`, which becomes a bus error, at `sdmmc.sysl:243-275`, `dma.sysl:111`, `stm32_spi.sysl:44-61`, `wm8994.sysl:63-68` (NACK on an unknown register, `:91`) and `dsi.sysl:107`. Real silicon ignores the write. This is a deliberate and useful policy, but it is stated only in `dsi.sysl:96-98`, not as the policy of the whole area.
- **DS3231 leap years.** `ds3231.sysl:32-34` applies the full Gregorian rule, but the real DS3231 treats 2100 as a leap year (any year divisible by 4). The guest-visible calendar therefore diverges in 2100.
- **DMA status registers.** `dma.sysl:93`: writes to LISR/HISR are accepted and ignored. The other models refuse such writes.
- **SD response checking.** `sdmmc.sysl:114` uses the card's own R1 error bits (`0xfdffe008`) to decide whether to start the data path. The real DPSM does not inspect the response.

**Better shape.** Each module's header lists its divergences, and the area README states the refuse-on-unsupported policy once.

### 12. MEDIUM — GPIO-level parts assume a 32-pin `u32` level mask
- `parts/spi/spi.sysl:12-14` (`require … < 32`)
- `hd44780.sysl:36-38`, `seven_segment.sysl:36` and `ssd1306`

`levels`/`driven` are `u32` bitmaps of `GP0` to `GP31`, so none of these parts can be wired to an STM32 port pin (`PA5`, `PI8`). That is why the STM32 boards reach SD only through a peripheral, never a bit-banged GPIO.

**Better shape.** Have the adapter resolve the part's pins to a small per-part view, so a part sees only its own pins (e.g. `[N]bool` in declaration order), not the MCU's pin space.

### 13. MEDIUM — the SPI SD card re-implements the SPI shifter that `parts/spi.Receiver` already provides
`sd_card.sysl:211-247` hand-rolls edge detection, bit counting and byte assembly. `parts/spi/spi.sysl:22-46` does the same but is receive-only, which is presumably why it was not reused.

**Better shape.** Extend `Receiver` into a full-duplex `Shifter` (MOSI in, MISO out, mode 0 to 3) and use it in both places.

### 14. MEDIUM — positional constructors with 20 to 40 arguments
- `sdmmc.sysl:57-59` passes 41 positional values, such as `0,0,0,0,0,0,0,false,[0;512],NEVER,0,…`.
- The same pattern is in `sd_card.sysl:40`, `dsi.sysl:33`, `mipi_dcs.sysl:33`, `hd44780.sysl:30` and `i2s.sysl:35`.

**Why it matters.** Adding a field means counting commas, and a swapped `0`/`NEVER` cannot be seen.

**Better shape.** Give fields defaults or use named arguments, so that only the non-default fields appear.

### 15. LOW — dead or diagnostic-only state inside the models
- `sdmmc.sysl:7`: `DATA_ERRORS` is never used.
- `data_base`, `data_index` and `transferred` are written but never read for behaviour. `data_length` is read only by a test (`double_tests.sysl:123`).
- `commands`, `transfers`, `blocks`, `buffer_completions`, `multi_transfers` and `double_transfers` (`sdmmc.sysl:37-52`) are instrumentation counters mixed into register state, and are read by `virt/recording_tests.sysl:78`.

**Better shape.** Delete the dead fields, and move the counters into a separate `Stats` struct.

### 16. LOW — `NEVER` is defined three times, and the raw literal is used three more times
- Defined: `sdmmc.sysl:5`, `sai.sysl:6` and `v6m/except.sysl:71`.
- Raw `0xffffffffffffffff`: `dsi.sysl:50` and `peripheral/spi/spi.sysl:105-108` (three uses).

**Better shape.** Use one shared timing constant, or better, `Option[u64]` for "nothing is due".

### 17. LOW — "MIPI DCS" part carries ST7789 vendor commands
`parts/mipi_dcs/mipi_dcs.sysl:76-81` gives argument counts for `0xB0`, `0xB2`, `0xB7`, `0xBB`, `0xC0`-`0xC6`, `0xD0`, `0xE0` and `0xE1`. These are ST7789 commands, not DCS, and the same part is instantiated for the DSI panel.

**Better shape.** Split the DCS core from a per-controller command table.

### 18. LOW — comments that name development steps, or are stale
- `sai.sysl:89` "outside this first subset"
- `wm8994.sysl:65` "First playback subset"
- `gt911.sysl:33` "The first model supports…"
- `dsi.sysl:107` "No video mode yet"
- `boards/stm32h747-*.hocon:1-3` "Initial runnable Discovery subset … remain to be implemented". This header is copy-pasted, and in `-sd`/`-spi-sd` it does not mention SD at all.
- `boards/h747-cores.hocon:32` "DSI NEXT", although DSI exists.
- `boards/pico2.hocon:1` "Description only for now"
- `mcus/rp2350.hocon:5`, which is stale (see finding 5).
- `peripheral/rp2040/README.md:9` still says "chip" after the `chip → mcu` rename. The rename also left `mcu    {` misaligned in `boards/pico.hocon` and `boards/pico2.hocon`.

### 19. LOW — `hd44780` pins are a `[7]int` indexed by position
`hd44780.sysl:28-66`: `pins[0]` is RS, `pins[1]` is RW (with `-1` as a sentinel), `pins[2]` is E, and `pins[3+bit]` are the data lines. `seven_segment` follows the same pattern (`segments: [8]int, commons: [8]int`).

**Better shape.** A `struct Pins { rs, rw: Option[int], e, d: [4]int }`.

### 20. LOW — sysl idiom: redundant suffixes, types and parentheses
- Redundant suffixes:
  - `1u16 else 0u16` (`peripheral/spi/spi.sysl:75`)
  - `if … then 1u32 else 0u32` throughout `spi.sysl:88-99`
  - `0usize..<4` and `0u32..<8` loop ranges (`dma.sysl:42`, `sd_card.sysl:65`, `native.sysl:34`, `wm8994.sysl:147`)
  - `0x404u16 else 0x808u16` (`wm8994.sysl:148-149`)
- `val c: &Card = Card(…); c` (`sd_card.sysl:40-41`, `ft6x06.sysl:26`, `wm8994.sysl:20`): bind and return.
- `u64(n.sector)`, where `sector` is already `u64` (`native.sysl:106`, `:115`).
- `(c.cr1 & 2) != 0`-style parentheses in the older files against the bare precedence in the newer ones.

### 21. LOW — 8-bit register pointers wrap through `+= 1`, which traps once sysl 0.8 debug overflow lands
`ft6x06.sysl:62` and `:68` (`t.pointer += 1` on a `u8`); `ds3231` has a similar pointer.

**Why it matters.** Wrap at `0xff` is the real auto-increment behaviour. Under the decided 0.8 rule (debug builds trap on overflow), it becomes a trap.

**Better shape.** Write the wrap explicitly with `+%`, or mask it.

### 22. MEDIUM (tests) — tests assert implementation with magic numbers and compound asserts
- `peripheral/sdmmc/tests.sysl:17-60`:
  - Calls such as `write(h,12,17 | 64 | 256 | 4096,0)` and `write(h,44,0x90 | …)` restate the same magic numbers.
  - Expectations are compound, like `assert(seen.copied && seen.bytes == 512 && h.remaining == 0 && h.idma == 1 && irq(h) && n.card.reads == 1)`: a failure does not say which clause broke.
  - The tests set private state directly (`n.state = 4`, `n.width = 4`) instead of driving CMD0/ACMD41/CMD2/CMD3/CMD7/ACMD6, so the card-initialisation path is not exercised through the host.
- `sd_card` (23 asserts) and `i2c` (7) cover little relative to their state machines. For example, the `native_abort` and CMD12-during-busy paths in `native.sysl` have no direct test (by grep).

**Better shape.** Use named register constants (finding 1), one assert per property, and a shared fixture that brings the card up through the real command sequence.

## sysl gaps suspected
- **No owned, inline fixed-capacity ring.** `sysl.container.ring.Ring` lays over caller-supplied storage through `*T`. A model that wants `[64]u32` embedded in its own `&T` struct would need a self-referential pointer, and `Deque` allocates. That is plausibly why every model hand-rolls `(head + count) % N`. The direct form would be something like `tx: Ring[u32, 64]` (const-generic capacity) or a `ring` constructor over a field of `self`. Not probed.
- **Register bitfields are NOT a gap.** `name: uW in u32` plus `union` (types.md:726, and §Unions, "a register read as a word or as its bits") is exactly the direct form, and benchtop uses it nowhere. Finding 1 is a code-quality problem, not a language problem.
- No other contortions that look forced by the language were seen. No probes were run.

## What is good
- Timing is done properly: rational clock accumulation (`sai.schedule`, `i2s.advance`) and lazy edge advancement that is invariant to call granularity. Several comments state that invariant, and the tests check it (advance to `due-1`, then to `due`).
- Models stay free of MCU addresses, UI and host filesystem access (e.g. `sd_card.Media` and the DSI `Endpoint`). The layering between part, peripheral and adapter is right.
- The board format is genuinely data, with a parameter schema in `registry.sysl`, rather than code.
- Unsupported modes are refused loudly instead of being half-emulated, which is the right default for an emulator people debug firmware against.
