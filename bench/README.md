# bench — how fast the core can dispatch

`benchtop bench` runs the ARMv6-M core (`sh.sysl.benchtop.v6m`) two ways over one guest: a `match`
loop over decoded records, and a chain of handlers joined by `become` (LLVM `musttail`). Both share
the decoder, the instruction semantics, the page-table memory path and the lazy N/Z flags; only the
step from one instruction to the next differs.

`guest/` is the workload: `kernel.c` (a bitwise CRC, a sieve, an insertion sort, a recursive
Fibonacci) built for the M0+, and `kernel33.bin` the same for the M33. Both are committed because
the tests embed them, and so are `dual.elf` and `dual33.elf`, the two-core workload below; `zsh
bench/guest/build.sh` rebuilds them all.

Run it from the repository's root, with the answer the same C computes on the host for 1000 rounds:

```
sysl build . -o benchtop
caffeinate -dimsu ./benchtop bench bench/guest/kernel.bin 1380735027
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
/usr/bin/time -l ./benchtop bench bench/guest/kernel.bin 1380735027 threaded
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

## The M33

`kernel33.bin` is the same `kernel.c` built for the Cortex-M33 without its DSP extension, and
`benchtop bench --core cortex-m33` runs it on the ARMv8-M Mainline core:

```
/usr/bin/time -l ./benchtop bench bench/guest/kernel.bin 1380735027 threaded
/usr/bin/time -l ./benchtop bench --core cortex-m33 bench/guest/kernel33.bin 1380735027 threaded
```

On the M5 Pro, sysl 0.5.7, five runs each, interleaved, on a machine shared with other work (so the
cycles are a range and the host instructions are not):

| core | guest instructions, three rounds | host instructions | per guest instruction | cycles per guest instruction |
|---|---|---|---|---|
| M0+ | 2002.6 M | 30.18 G | 15.1 | 3.3–3.5 |
| M33 | 1762.5 M | 27.04 G | 15.3 | 3.6–3.8 |

Thumb-2 says the same in 12% fewer instructions, and each costs about what an M0+ instruction does,
so a round takes about as long on either. The M0+'s count is the same as before the M33 existed.

`it33.bin` (`it.s`) is a loop in which 12 of 27 instructions are inside IT blocks of every length;
`it.c` computes its answer on the host:

```
/usr/bin/time -l ./benchtop bench --core cortex-m33 bench/guest/it33.bin 830458893 threaded
```

On the M5 Pro, sysl 0.7.10, three rounds of 270.0 M guest instructions, the `become` chain retires
13.58 G host instructions, 16.8 per guest instruction: an instruction in a block costs its condition's
handler and the one jump to its own, chosen when the block was decoded.

## The Hazard3

`kernel-rv.bin` is the same `kernel.c` built for RV32IMAC (`start-rv.S` sets the stack, calls `run`
and ends with `EBREAK`), and `benchtop bench --core hazard3` runs it on the RISC-V threaded
executor (`riscv/threaded.sysl`), or with `interpret` after the expected value on the reference
interpreter, which decodes every step:

```
/usr/bin/time -l ./benchtop bench --core hazard3 bench/guest/kernel-rv.bin 1380735027
/usr/bin/time -l ./benchtop bench --core hazard3 bench/guest/kernel-rv.bin 1380735027 interpret
```

On the M5 Pro, sysl 0.7.8, three rounds of 429.3 M guest instructions:

| | host instructions | per guest instruction | MIPS |
|---|---|---|---|
| a priority scan of every interrupt and of every PMP region per access, byte-wise RAM | 1606.0 G | 1247 | ~16 |
| the interpreter (`interpret`): a cached interrupt line, decoded PMP with a Machine-mode fast path, word RAM access | 288.9 G | 224 | ~125 |
| now: a decode cache and a threaded executor | **29.05 G** | **22.6** | **~790** |

The threaded executor's handlers run only in Machine mode with no PMP region binding it and no
interrupt due, and every one checks the budget, so it stops exactly where the interpreter does;
CSRs, system instructions, atomics and Zcmp go through the interpreter one step at a time. The M0+
threaded line is unchanged by it (42.00 G before, 41.99 G after).

## Two cores

`guest/dual.c` runs on the virtual board: each core folds the CRC over a 4 KB buffer of its own a
thousand times, with nothing shared, then core 1 sends its result through the FIFO and core 0 exits.
`bench-board` runs it three rounds with the cores taking turns on one thread and three with a thread
each, and reports both cores' instructions over the wall time:

```
caffeinate -dimsu ./benchtop bench-board bench/guest/dual.elf
/usr/bin/time -l ./benchtop bench-board bench/guest/dual.elf threads
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

`dual33.elf` is `dual.c` built for the M33, which `bench-board --board virt33` runs on two M33s:
368.7 M guest instructions a round against the M0+ build's 573.5 M, in about 0.20 s taking turns
and 0.12 s on a thread each (single runs, the same loaded machine), against the M0+'s 0.42 s and
0.24 s.


## Persistent desktop CPU workers

`sysl test . --filter 'CPU worker throughput:' --timeout 30` measures the two
persistent host workers using the committed `dual33.elf` workload. It writes
`target/worker-throughput.txt`, with per-core retired counts, elapsed wall time,
and per-core and aggregate MIPS. Creation of the workers is outside the measured
interval. Guest startup and decoding are included. This test runs unthrottled.

On an Apple M5 Pro (18 CPUs), sysl 0.6.2, October 26, 2026, median of three runs:

| Model pair | Core 0 MIPS | Core 1 MIPS | Aggregate MIPS |
|---|---:|---:|---:|
| M33 / M33 | 1237 | 1237 | 2474 |
| M7 / M7 | 1259 | 1259 | 2518 |

Both use the same Thumb-2 integer CRC workload; this does not measure an M7-tuned
build, audio, framebuffer rendering, bus contention, or hardware cycle accuracy.

Set `BENCHTOP_CPU_TRACE=1` when launching a windowed session for approximately
one-second samples of per-core MIPS, launched status and UI FPS. The existing
150 MHz touchscreen demo measured approximately 150 MIPS on core 0 and zero on
its unlaunched core 1, while the UI ran at approximately 120 FPS. This is a live
paced polling workload, distinct from the unthrottled benchmark above.

## Real time on the STM32H747I-DISCO

The bar: both cores of the H747 emulated at real time with the DSI display and the audio running.
`benchtop realtime` runs a firmware on the CPU workers exactly as the desktop does (each core on its
own host thread, paced against the host's clock, DSI packets applied by the DSI worker), with no
window. The main thread plays the desktop's part: it takes the audio a sound card would, feeds the
microphone a 440 Hz tone whose loudness swells once a second, drags a finger along the slider, and
copies the 800 x 480 panel out sixty times a second as the window paints it.

```
sysl build . -o benchtop
caffeinate -dimsu ./benchtop realtime --seconds 10 examples/touch/mic-disco.elf
caffeinate -dimsu ./benchtop realtime --seconds 10 --unpaced examples/touch/mic-disco.elf
```

`mic-disco.elf` is the firmware that exercises the most at once: the M7 takes the WM8994's DMIC
input over SAI1 B and DMA, filters it and fills the speaker's buffer; the M4 runs imui and PlutoVG,
draws the input meter as the level moves, and sends changed pixel runs to the panel over DSI. The
board models no LTDC and no DMA2D, so the M4 draws every pixel itself and the panel takes DCS
command-mode packets; video-mode streaming is not part of this check.

It prints, per core, guest seconds over host seconds (1.000 is real time) and how busy that core's
worker was (the host time it did not spend waiting for the clock); the DSI worker's and the display
copy's share of the host time; the audio rates; and exits 1 where a core falls below real time.
`--unpaced` runs the cores in lockstep as fast as the host can, for the headroom.

On the M5 Pro, sysl 0.7.8, 10 guest seconds after 1 of boot, on a machine loaded with other work
(load average 6–9), three runs each:

| | M7 (480 MHz) | M4 (240 MHz) | DSI worker | display copy | audio out / in |
|---|---|---|---|---|---|
| paced | 1.000 x, 3.5 MIPS, busy 4–5 % | 1.000 x, 153 MIPS, busy 45–57 % | 0.6–0.8 % | 3.6–4.2 % | 48 000 / 48 000 frames/s |
| unpaced (lockstep) | 1.31–1.40 x | 1.31–1.40 x | | | |

`ps -M` agrees with the busy column: over the same ten seconds the M4's thread used 4.7 s of
processor, the M7's 0.4 s, the main thread 0.4 s and the DSI worker 0.15 s. Both cores keep real time
with the M4 at about half a host core.

A profile of that run (`sample <pid> 5 1`) finds no pixel operation on either guest core's thread.
The M4's thread spends its time on guest instructions and on the guest's own MMIO: the DSI status
polls while a packet crosses the link, the I2C touch polls. Every pixel the host touches is touched
on the DSI worker (DCS writes into the panel's memory) or on the desktop's thread (the copy out and
the texture). The copy out held the panel's lock while it converted all 384 000 pixels, about a
millisecond, and the DSI worker waited behind it (2.0–2.4 % busy, much of it blocked); it now copies
whole rows under the lock and converts after, and the DSI worker is at 0.6–0.8 %.

### A busy M7

`mic-disco.elf` leaves the M7 asleep most of the time, so a slowdown in heavy M7 code would not show
there. `busy-disco.elf` (`examples/touch/build.sh busy`; [what it runs](../examples/touch/README.md#a-busy-m7))
is the same firmware with an M7 that never sleeps: a 255-tap FIR in its audio interrupts and, between
them, kernels for memory across three regions, double precision, unpredictable branches and
peripheral polls. `--set busy_kernel=N` runs one kernel alone (1 memory, 2 double, 3 branches,
4 polls, 5 none), so a fall below real time names its category.

```
caffeinate -dimsu ./benchtop realtime --seconds 10 examples/touch/busy-disco.elf
caffeinate -dimsu ./benchtop realtime --seconds 5 --unpaced --set busy_kernel=4 examples/touch/busy-disco.elf
```

Each core's line gives the guest MHz and MIPS it ran at, and the MHz its worker could have sustained:
what it ran at over the share of the window it was busy (unpaced, simply what it ran at).

On the M5 Pro, sysl 0.7.8, 2026-10-28, on a machine saturated by other work (load average 21–26: the
unpaced `mic-disco.elf` reference measured 0.17–0.19 x in the same minutes, against 1.31–1.40 x
above, so these are a floor, not the quiet-machine figures), two rounds:

| | M7 | M4 |
|---|---|---|
| paced, all kernels | 1.000 x, 272 MIPS, busy 78 % (could sustain 617 MHz); one round 0.685 x | 1.006 x, 154 MIPS, busy 89 %; one round 0.471 x |
| `mic-disco.elf` paced, same minutes | 1.000 x both rounds | 1.001 x; one round 0.773 x |

The M7 alone, unpaced (the cores in lockstep, the M4 drawing as above):

| kernel | M7 sustained | M7 MIPS |
|---|---|---|
| all, in turn | 364–387 MHz | 207–220 |
| 1 memory | 244–279 MHz | 172–197 |
| 2 double | 263–373 MHz | 49–69 |
| 3 branches | 140–212 MHz | 89–134 |
| **4 peripheral polls** (before the quiet reads below) | **49–58 MHz** | **38–45** |
| 5 none (asleep between interrupts) | 102–153 MHz | 9–14 |

The polls were the limit, by a factor of five: a profile (`sample <pid> 5 1`) put two thirds of the
M7's thread in `virt.sai_dma_read`, because every SAI or DMA register read brought the audio up to
the core's time, serviced the DMA through two freshly allocated bus closures, and published the
result. A read now answers from the devices' state when no slot falls due by its time and the DMA
has nothing left to move (`quiet_audio_read`): bringing the devices to that time would only move
their clocks, so the state the last access published is the state at the read's time, and the
values read are the same (`sai_dma_polls` checks polls every 7 instructions against polls every
1400). The DMA's beats are made without callbacks (`dma.next_beat` / `dma.finish_beat`), and the slot
callbacks are made only when a slot is due. Interleaved with the build before it, unpaced, three
guest seconds, on a machine loaded by other work (so the MHz move with the load; the host
instructions retired over the whole run do not):

| kernel | before: M7 MHz, host instructions | after |
|---|---|---|
| 4 peripheral polls | 75–76 MHz, 768–784 G | **139–201 MHz, 221–224 G** |
| 1 memory | 199–520 MHz, 132–144 G | 226–612 MHz, 128–144 G |
| 2 double | 299–568 MHz, 123–132 G | 265–623 MHz, 124–134 G |
| 3 branches | 230–390 MHz, 129–145 G | 194–398 MHz, 125–143 G |
| all, in turn | 297–501 MHz, 134–151 G | 508–591 MHz, 131–134 G |

What the polls still pay is the audio lock: the M4's per-slice `signal_sai_dma` brings the audio in
whenever its clock passes a slot first, so the two cores' threads meet on the lock (`sysl_futex_wait`
is most of what is left in `sai_dma_read`), and the DSI status poll meets the M4 on the DSI host's
lock the same way. Bringing a device in only on the core that drives it removes that, but it moves
where the other core's slices end, and with them the instruction counts the cores reach by a given
frame, so it is not done. In the mixed run the polls are a small share of the work, and the profile is
instead the M7's loads and stores (`h_ld_imm`, `h_ldr_imm`, `h_ld_reg`, `h_fp_ldr` → `fp_words`, and the stores: about
two fifths of its thread), with 22 % in `run_core`'s per-slice `signal`: `signal_dsi` 13 % and
`signal_sai_dma` 8 %, on a core that drives neither. The sleeping kernel runs slower than the busy
ones because, in lockstep, every wake-up is a thread hand-off, which a loaded machine makes slow.
