# bench — how fast the core can dispatch

`cortical bench` runs the ARMv6-M core (`sh.sysl.cortical.v6m`) two ways over one guest: a `match`
loop over decoded records, and a chain of handlers joined by `become` (LLVM `musttail`). Both share
the decoder, the instruction semantics, the page-table memory path and the lazy N/Z flags; only the
step from one instruction to the next differs.

`guest/` is the workload: `kernel.c` (a bitwise CRC, a sieve, an insertion sort, a recursive
Fibonacci) built for the M0+. `kernel.bin` is committed because the tests embed it, and so is
`dual.elf`, the two-core workload below; `zsh bench/guest/build.sh` rebuilds both.

Run it from the repository's root, with the answer the same C computes on the host for 1000 rounds:

```
sysl build . -o cortical
caffeinate -dimsu ./cortical bench bench/guest/kernel.bin 1380735027
```

On an Apple M5 Pro, sysl 0.5.4, 667.5 M guest instructions, on a quiet machine:

| executor | MIPS |
|---|---|
| `match` loop | ~1195 |
| `become` chain | **~1365** |

That is about ten times an RP2040 core's real speed.

Wall time moves with whatever else the machine is doing. The per-process counters do not, so a
change is judged by them. Name one executor so the counters see it alone:

```
/usr/bin/time -l ./cortical bench bench/guest/kernel.bin 1380735027 threaded
```

and divide `cycles elapsed` and `instructions retired` by three rounds of guest instructions. For
the `become` chain, sysl 0.5.5:

| | cycles / guest insn | host insns / guest insn |
|---|---|---|
| before `CMP`+`B<cond>` fusion, lazy V and the page-entry test | 3.83 | 16.0 |
| now | **3.33** | **13.7** |

The handler for `EORS` is ten AArch64 instructions with no frame, ending in `ldr x3, [x1, #0x10]!`
and `br x3`. Guest RAM accesses are relaxed atomics, so that two host threads may make them at once
(DESIGN.md §1.5); on AArch64 a relaxed load or store is the plain `ldr` or `str`, and the counters
did not move when they became so (30.18 G host instructions for three rounds before and after).

## Two cores

`guest/dual.c` runs on the virtual board: each core folds the CRC over a 4 KB buffer of its own a
thousand times, with nothing shared, then core 1 sends its result through the FIFO and core 0 exits.
`bench-board` runs it three rounds with the cores taking turns on one thread and three with a thread
each, and reports both cores' instructions over the wall time:

```
caffeinate -dimsu ./cortical bench-board bench/guest/dual.elf
/usr/bin/time -l ./cortical bench-board bench/guest/dual.elf threads
```

On the same M5 Pro, sysl 0.5.7, 573.5 M guest instructions a round, on a quiet machine:

| mode | aggregate MIPS | wall, three rounds | cycles elapsed, three rounds |
|---|---|---|---|
| turns, one thread | ~1350 | 1.27 s | 5.47 G |
| `--threads`, a thread each | **~2420** | **0.71 s** | 6.06 G |

The threads finish the same work in 0.56 of the time, about 1.8 times the speed. They spend 11%
more cycles altogether: each core's run ends every quantum to publish its clock and look at the
other's, and the two cores' threads share a cache line or two of the board's state. On a loaded
machine the threaded mode gives back much of its gain — a core that gets 4000 instructions ahead of
one the host has descheduled blocks until it runs again.
