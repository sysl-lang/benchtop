# benchtop review — AREA = cores (v6m/, riscv/, fpu/cycles/nvic)

**Verdict.**
- The Arm threaded core is fast, and its decode is well structured. This branch adds a second dimension, `TIMED`, by hand, and the new memory map is module-level globals that every hot path now consults. Each new `Op` must now be added in five places that nothing keeps in sync.
- The RISC-V core is a separate, readable, slow interpreter that shares nothing with the Arm side. It is RP2350-specific despite claiming to be "one interpreter by ISA capability", and **no board runtime calls it**: only the catalog and tests do.
- The NVIC extension and the flash/RAM-bank work are layered on as patches ("keep the original low words", sentinel offsets, guard records). They work, but they make the core harder to reason about than a uniform redesign would have.

---

## Findings (most severe first)

### 1. HIGH: the RISC-V core is not wired into any board, and has no bus
- `riscv/cpu.sysl:65-86`: `read` and `write` reach only a flat `ram` slice. Any other address is `Stop.Unmapped`.
- Outside `riscv/`, `sh.sysl.benchtop.riscv` is imported only by `core_options.sysl:6` and `registry.sysl:5`, which use `model_named`, `implemented` and `MODELS`. Nothing in `virt/` constructs or runs a RISC-V `Cpu`.
- The branch is named `pico2-riscv`, yet a Pico 2 RISC-V image touching any peripheral cannot run.
- **Why it matters:**
  - About 2.6k lines of core and tests are reachable only from tests.
  - The interfaces a board needs are absent, so the core's shape was never tested against real use: an MMIO hook, a page table, a clock/limit contract and the `Stop` mapping that `virt` expects.
- **Better shape:** before more ISA work, give the RISC-V `Cpu` the same bus contract as `v6m` (an `io_read`/`io_write` pair, or the page table and slow path) and one `virt` path that runs it. Alternatively, say plainly in the module header that it is a test-only ISA model.

### 2. HIGH: the memory map is process-global state the core reads on every access
- `v6m/cpu.sysl:47-62` (diff): `ram_base`, `ram_size`, `flash_base`, `flash_size`, `flash[4 MB]`, `extra_ram[7]` and `extra_ram_count` are module `var`s.
- `ram_storage_offset` (cpu.sysl, the `stored`/`watch` path) reads the global `page_table` rather than `cpu.pages`. There are now two sources of truth for the mapping.
- `decoded` and `decoded_own` grew from 524,289 to 2,621,442 16-byte records each, about 84 MB of static storage. `reset` clears only the RAM part of `decoded`. `pair` clears the RAM and flash parts of `decoded_own`, and must stay in step with `reset_mapped` by hand.
- **Why it matters:**
  - Two machines in one process are impossible: a test that builds an M33 machine after an H747 inherits `flash_size` unless `reset` remembers to zero it, which it does today by hand.
  - The core knows a board's memory layout.
  - `reset_mapped` (cpu.sysl, end of file) does not check `flash_length <= FLASH_BYTES`, `ram_length <= RAM_BYTES` or `image.len >= RAM_BYTES + flash_size`. It asserts only bank alignment. An oversized board description indexes past `flash`, or past `image`.
- **Better shape:**
  - A `Memory` struct, owned by the machine and pointed to from `Cpu` (it already carries `pages`), holding the map, the banks and the flash backing.
  - Validate lengths in `reset_mapped` with `require`, the way `banks.len` already is.

### 3. HIGH: every new `Op` must be written in five places, and nothing keeps them in sync
1. The `Op` enum (`decode.sysl:10`).
2. `handler_of_timing` (`threaded.sysl:399-551`, ~150 arms).
3. The `match` executor (`switch.sysl:50+`).
4. `instruction_cycles` (`cycles.sysl:8-22`).
5. The hand-maintained op list in `configure_instruction_timing` (`cycles.sysl:37-50`). This repeats every op that `instruction_cycles` matches, so a multi-cycle op added to the `match` but forgotten in the list is silently costed at 1 cycle.

- **Why it matters:** this is the shape most likely to rot. Item 5 is a pure duplication with a silent failure mode.
- **Better shape:**
  - Fill `cycle_costs` by iterating every `Op`. Use `for const c in Op::Variants`, which the reference documents in `generics.md` § "A `for const` over a type's own make-up", or loop `0..<256` and convert. The list then disappears.
  - Longer term, one table per op (handler, cost, record length) would replace the parallel matches.

### 4. HIGH: the hand-unrolled const-generic dispatch ladders, now doubled by `TIMED`
The pattern `cond match 0 -> &h_x[0, TIMED] … 13 -> &h_x[13, TIMED]` appears at:
- `threaded.sysl:364-396` (two 14-arm ladders);
- `t2threaded.sysl:48-82` (two 15-arm ladders) and `129-143`;
- `t2threaded.sysl:402-573`: `dp_reg_handler` 28 arms, `shifted` 28 arms × 5 instantiations, plus the `ld_*`, `bcond_w`, `bit` and `extend` ladders;
- the `fp_handler_s`/`_d` ladders in `fpu_exec.sysl`, re-cut with `TIMED`.

About 400 lines are `N -> &h[N, …]`.

- **Why it matters:** a typo such as `9 -> &h[8]` is undetectable by reading and is caught only if a test hits that exact condition and opcode.
- **Better shape:** `for const k in 0..<14 do if cond == k then return &h_bcond[k, TIMED]`, or build a `[14]Handler` table once. `for const` over a range of up to 64 values is documented in `generics.md` § "What a `for const` will not do", and `return` inside it is explicitly allowed.
  - If taking `&h[k, T]` inside a `for const` body is refused, that is a sysl gap and goes on the gaps list (see below). Probe it.

### 5. MEDIUM: `TIMED`, `V8` and `SINGLE` are `const …: u32` used as booleans
- Locations: `threaded.sysl:36` and every `h_*[const TIMED: u32]`; `switch.sysl:23` (`run_match_of[const V8: u32, const SINGLE: u32]`); `cycles.sysl:68`. Call sites read `jump[1]`, `handler_for_timing[0]`, and tests read `if TIMED != 0`.
- `generics.md:1528` documents `bool` value parameters (`struct Flag[const B: bool]`).
- **Better shape:** `[const TIMED: bool]`, with call sites written `jump[true]`. It reads as intent, and a stray `2` cannot type-check.

### 6. MEDIUM: the timing switch is checked at decode time in three different spellings, and IT blocks re-dispatch at run time
- The spellings:
  - `decode.sysl:300`: `if CYCLE_ACCOUNTING && cpu.instruction_timing then fused_handler_of_timing[1] … else …[0]`;
  - `thumb2.sysl:143`: the same expression, for `cmp_w_bcond`;
  - `t2threaded.sysl:588`: `selected_handler`.
- `handler_of`, `fused_handler_of`, `handler_for` and `cmp_w_bcond_handler` (`threaded.sysl:554-555`, `t2threaded.sysl:585-586`) are the non-timed wrappers, kept beside them.
- `h_pred` (`t2threaded.sysl:41`) calls `selected_handler(cpu, ip)` **every time a predicated instruction runs**. That is a full `match` on `ip.op` plus a family sub-match, inside the hot path, so IT blocks lose the very threading the file exists for.
- `CYCLE_ACCOUNTING` (`cycles.sysl:6`) is a `const true` that is never false, so it is a dead switch tested in eight places.
- **Better shape:**
  - One `handler_for(cpu, ip)` used everywhere.
  - Bake the predicated instruction's inner handler at decode time: there is room in a second record or in `imm`, or use an `h_pred[K, L, TIMED]` that `become`s a pre-selected handler stored when decoding.
  - Delete `CYCLE_ACCOUNTING`.

### 7. MEDIUM: the RISC-V core is RP2350-specific while claiming to be generic
- `models.sysl:3-4` says "one interpreter, selected by ISA capabilities". But:
  - `state.sysl:3`, `:35-38`: reset loads RP2350 boot-ROM PMP regions for every model.
  - `csr.sysl:18`, `:33-35`: `misa`, `mvendorid`, `marchid` and `mimpid` are RP2350 constants returned for any EXT_CSR model.
  - Behaviour is selected by `cpu.model.name == "hazard3"` at `state.sysl:71`, `:76` and `:160`. That is a string compare in the reservation path and in `take_interrupt`.
  - Errata E6 and E7 are hard-wired.
- **Better shape:**
  - Extend `Model` (or add a `Chip` record) with `reservation_granule`, `mie_masks_umode`, the ID CSRs and the reset PMP regions.
  - Test flags, not names.
  - Move the RP2350 specifics to the board.

### 8. MEDIUM: RISC-V per-instruction cost is far from "real-world speed"
Each `run` iteration (`riscv/cpu.sysl:257-305`) does the following:
- `take_interrupt` → `pending_bits` → `next_irq`, a **52-iteration priority scan** (`state.sysl:124-133`) on every instruction, even with nothing pending.
- One to three `pmp_allows` scans of 16 regions, each with a bit-counting `while` loop (`state.sysl:192-197`). Every fetch half, every load and store, plus a third explicit check for 32-bit fetches.
- Byte-by-byte `for` loops for every load, store and fetch (`cpu.sysl:74`, `:86`).

There is no decode cache.

- **Better shape:**
  - Keep a cached `pending & enabled` summary, updated on CSR or `irq()` writes, and test it for zero.
  - Precompute PMP `[lower, upper)` on `pmpaddr`/`pmpcfg` writes, with a "no regions active" fast path.
  - Use word loads.
  - If the RISC-V core is meant to run firmware, it needs the same page-table and threaded treatment as v6m. Otherwise the coverage docs should say "reference interpreter".

### 9. MEDIUM: NVIC banks use three indexing schemes
`v6m/cpu.sysl:296-305` and `v6m/nvic.sysl`:
- `enabled` is a `u32` of IRQs 0–31.
- `enabled_extra[15]` holds `u32`s of IRQs from 32 up.
- `pending`/`active` are `u64`s of *exception numbers* 0–63, while `pending_extra[7]` and `active_extra[7]` hold exceptions from 64 up.

Hence:
- `nvic.sysl:73-74`, which splices three words with shifts of 16 and 48 to line enables up with pending bits;
- `except.sysl` (diff), which splices `enabled_extra[0] & 65535 << 48` in the low path.

Other problems in the same file:
- `7`, `15` and `64` appear raw throughout. `bank * 32` is used in some places and `/64 - 1` in others.
- `extra_running_priority` (`nvic.sysl:62`) calls `group(c, priority(c, e))` twice.
- Both scans shift bit by bit instead of using `trailing_zeros`.
- `nvic_read` (`:116-117`) computes a 32-iteration `bank_bits` and then discards it for ISER/ICER.
- The low `incoming` word uses `SeqCst` while the extra banks use `Release`/`Acquire`.

- **Better shape:**
  - Uniform exception-indexed `[8]u64` arrays for enabled, pending, active and incoming, with one summary mask each.
  - The "cheap common path" is then `word[0]`, at the same cost.
  - Name `NVIC_BANKS` and `NVIC_MAX_IRQS`.

### 10. MEDIUM: flash write protection is an extra compare on every store
- `cpu.sysl` `store`/`write` (diff) and `t2sem.sysl` (diff) each add `if flash_range(a, 1) then halt(…Fault…)` ahead of the page-table fast path.
- **Why it matters:**
  - It costs one more global load and two compares on every guest store.
  - The width check is `1`, so a 4-byte store straddling the start of flash is not caught by this test.
- **Better shape:** a separate write page table (or a read-only tag bit in the page entry) so that flash pages miss on store and the existing slow path faults them, with zero hot-path cost.

### 11. MEDIUM: sentinel and guard-record arithmetic in place of named concepts
- `FLASH_OFFSET = RAM_BYTES + 2 // Guard record` (`cpu.sysl:48`).
- `extra_ram_offset` returns `RAM_BYTES` as "not found", and `code_offset` returns `MEMORY_BYTES` as "not found".
- `code_address` uses a raw `+ 4096` guard window, and `install_ram_guards` hard-codes `2048` records.

These recur at about 15 call sites as `code_offset(x) == MEMORY_BYTES` or `< MEMORY_BYTES`. Callers also mix `==` and `<` for the same test: `thumb2.sysl` uses both, and `decode.sysl:267` uses `==`.

- **Better shape:**
  - `const NO_CODE = MEMORY_BYTES` and `const GUARD_BYTES = 4096`, with `GUARD_RECORDS = GUARD_BYTES / 2`.
  - An `is_code(a)` predicate.
- **Message bug:** `require banks.len <= 7, "at most eight RAM banks"` (`reset_mapped`). The message counts the primary bank and the check does not; say "at most seven extra RAM banks".

### 12. MEDIUM: `Kind`, `Arch` and the `is_*` predicates are three overlapping classifications
- `models.sysl` `Model.arch` already says `V7m`/`V8mMain`. Yet `is_v7m` (`:62`), `is_v6m`, `is_baseline` and `has_fixed_dsp` re-list kinds by hand, and `cycles.sysl:12-20` and `except.sysl:330` compare `Kind.V7emM7`/`Kind.M3` directly.
- `Kind.V7em` means "Cortex-M4" and `V8mMain` means "Cortex-M33": architecture names used as core names, kept because "existing discriminants stay unchanged" (`cpu.sysl:157`).
- `fpv5(cpu) = cpu.kind != Kind.V7em` (`fpu.sysl:247` and `:265`) is an inverted test: any future kind is assumed FPv5.
- The `Model` fields `fpu: string` ("none"/"sp"/"dp") and `irqs: int` are stringly or loosely typed, and the constructor takes eight positional scalars, unreadable at `models.sysl:32-43`.
- **Better shape:**
  - Derive the predicates from `model_of(kind).arch` once, at reset, into `Cpu` flags.
  - Add an `fpu: FpuKind` enum and an explicit `fpv5: bool`.
  - Rename the kinds after cores, or at least comment the mapping.

### 13. MEDIUM: `h_dsp` re-checks DSP at run time against a second source of truth
- `t2threaded.sysl:579-581` checks `cpu.dsp`. Decode checks `model_of(cpu.kind).dsp` (`dsp.sysl:54`, `thumb2.sysl:693`).
- The comment "Shared decoded records may run on cores with different extension choices" implies records are shared across cores. But then the `TIMED` handler baked by one core's `instruction_timing` also runs on the peer.
  - Integer costs degrade safely: an untimed core's `cycle_costs` are all 1.
  - `retire_fp[TIMED]` → `instruction_record_cycles` charges FP cycles **regardless of `cpu.instruction_timing`** (`cycles.sysl:55-60`, `:73-75`). An untimed peer running a timed-baked FP record gets non-unit cycles.
- **Better shape:**
  - One DSP flag, on `Cpu`.
  - Have `instruction_record_cycles` return 1 when timing is off, or never share records across cores with different decode-time choices.

### 14. MEDIUM: duplicated bit utilities and hand-rolled loops where the standard library has the operation
The standard library's `sysl.math.integer` (`library/sysl/math/integer.sysl:92-127`) provides `count_ones`, `leading_zeros`, `trailing_zeros`, `reverse_bits` and `rotate_left`/`rotate_right`. The code hand-rolls them instead:
- `riscv/bitops.sysl:5-19`: `rotate`, and `count_bits` as 32-iteration loops for `clz`/`ctz`/`cpop`.
- `bitops.sysl:61-68`: `orc.b`, `rev8` and `brev8` as loops.
- `v6m/t2sem.sysl:338-366`: `clz` and `rbit` by hand.

Sign-extension and field helpers are also defined several times:
- `sext`: `v6m/decode.sysl:245`, `riscv/cpu.sysl:88`, and `riscv/compressed.sysl:16` (as `cx`);
- `bits` and `fbit`: `decode.sysl:241`, `fpu.sysl:261`;
- `bad` and `dsp_bad`: `thumb2.sysl:46`, `dsp.sysl:60`;
- `st` and `store_code`: `compressed.sysl:9`, `zc.sysl:22`. `st` is `store_code` with `op = 2`.

- **Better shape:** use the library methods (they compile to `ctlz`/`ctpop`), and keep one `sext`/`bits` per module.

### 15. MEDIUM: boilerplate handler bodies
About 45 handlers in `threaded.sysl:212-314` and `t2threaded.sysl:276-340` have the same body:

```
if x_foo(cpu, ip, n) then become next(cpu, &ip[1|2], retire_timed[TIMED](cpu, ip, n))
stop_access(cpu, ip, n)
```

Each differs only in the callee and the record length.

- **Better shape:** a single `h_mem[const OP: Op, const L: u32, const TIMED: bool]` whose body does `match OP` (folded at compile time) to call the right `x_*`. A function-valued const parameter is not documented, so the `match`-on-const form is the idiomatic route.

### 16. MEDIUM: RISC-V `execute` mixes decode, validation and commit with implicit channels
- `cpu.access` (0 fetch, 1 load, 2 store) is a mutable side channel. `run`, `step`, `execute`, `atomic_operation` and `execute_zcmp` set it, `read` reads it, `write` ignores it (hard-coded `2`), and `resolve_fault` reads it to choose the cause.
- The power instructions (`cpu.sysl:127-141`) duplicate the retire and sleep tail of `execute` (`:218-224`).
- `execute_zcmp` (`zc.sysl:31-72`) retires on its own and bypasses the target-misalignment check that `execute` does (`:214-217`): `cm.popret` sets `pc = ra & ~1` without the `alignment` test.
- Magic CSR and status bits are everywhere: `0x200000` (TW), `0x20000` (MPRV), `0x1888`, `0x800`, `0x888`, `0x8000000B`, `0xBE0`…`0xBE5`, `0x30200073`/`0x10500073`/`0x00102033`. Compare `fpu.sysl`, which names its CSR addresses and FPSCR fields.
- **Better shape:**
  - Pass the access kind as an argument (an `enum Access { Fetch, Load, Store }`).
  - Route every retirement through one `retire(cpu, next)`.
  - Name the CSR numbers and the `mstatus` fields.

### 17. MEDIUM: `Stop.Budget` means "running"
`riscv/cpu.sysl:9` (and v6m) compare `cpu.stop == Stop.Budget` about 30 times to mean "nothing has stopped".
- **Better shape:** a `Running`/`None` variant, or `stop: Option[Stop]`. In RISC-V, `run` "resuming from Sleep" sets `Stop.Budget`, which reads as "out of budget".

### 18. LOW: RISC-V `Model` fields that nothing reads
`riscv/models.sysl:20-24`:
- `irqs` is never used. `Machine.priority` is hard-coded `[52]u8` and `irq()` tests `number >= 52`.
- `isa` is read only by a catalog test.

Use `model.irqs` for the bounds check, or drop the field.

### 19. LOW: Python-generated tests addressed by hard-coded record ranges
- `riscv/instruction_tests.sysl` calls `check_instruction_vectors("BEQ", 706, 1000, 0)` and so on. Adding one vector upstream shifts every later range, and the failure would point at the wrong mnemonic.
- `instruction_harness_tests.sysl` duplicates the profile catalogue: `["rv32i", …, "hazard3"]` and `extensions = [0, 1, 4, 5, 4095]`, where `4095` is the current `HAZARD3` mask.
- **Better shape:**
  - Store a mnemonic id per record and filter by it.
  - Iterate `MODELS` and use `model.extensions`.

### 20. LOW: tests that restate the cost table
`cycles_tests.sysl:80-82` asserts `fp_instruction_cycles(...) == 1/3/28`, which is the table read back. The executor-level assertions in the same file, cycles after running real code, are the valuable ones. Drop the echo tests, or source the numbers from the cited TRM table in a comment.

### 21. LOW: comments that record history rather than invariants
- `cpu.sysl:157` "Existing discriminants stay unchanged".
- `cpu.sysl:296` "Keep the original low NVIC words".
- `nvic.sysl:6` "Low words remain in their original fields".
- `cycles.sysl:5` "Unit timing remains the default".
- `cycles.sysl:53-54` "The common one-cycle ALU handlers remain unchanged. No growth of the 16-byte decoded instruction".
- `switch.sysl:3-11` has a stray empty `//` splitting its header.

Rewrite these as what the code guarantees (rule 6).

---

## sysl gaps suspected
- **Taking a const-generic handler's address from inside `for const`** (`for const k in 0..<14 do if cond == k then return &h_bcond[k, TIMED]`). The reference says `for const` over a range of 64 or fewer unrolls and permits `return`, so the 400 lines of ladders should be unnecessary.
  - **Not probed** (no slot taken). If it is refused, it is a gap. If it works, the ladders are a pure readability defect.
- **Function-valued const parameters** (`h_mem[const F: fn(...)]`) are not documented. The `match`-on-const-`Op` form in finding 15 is the workaround-free route, so this is only a gap if that `match` does not fold.
- **No gap is suggested by the `u32`-as-bool const parameters.** `bool` value parameters are documented at `generics.md:1528`, so that is an idiom finding only.

## What is good
- The threaded design is the right one for speed, and its file-header explanations are unusually clear:
  - one native frame;
  - `become`;
  - the budget checked only at taken branches;
  - compare+branch fusion;
  - a `match` executor kept as an independent reference.
- The Thumb-2 decoder (`thumb2.sysl`) is split along the ARM ARM's encoding groups and is easy to follow.
- The RISC-V core is small and readable, and its tests are thorough: vectors are generated independently of the interpreter, and an LLVM-assembled compressed-instruction oracle is included.
- CSR validation before side effects is careful and commented against the RTL.
- The FPU uses LLVM intrinsics and named FPSCR fields, which shows how the rest of the code could look.
