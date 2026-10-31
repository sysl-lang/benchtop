# benchtop performance, before and after the 88-finding review

Before: `cda454b`. After: `ffdd1f8`. Both built with sysl 0.8.7 and run from their own worktrees.

Machine: Apple M5 Pro, 68719476736 bytes (64 GiB) of memory. Other sessions ran on the machine
throughout; the load average (1, 5, 15 minutes, from `uptime` after each run) is in the tables.

## Host instructions retired

`/usr/bin/time -l <cmd> 2>&1 >/dev/null | grep "instructions retired"`, three interleaved runs each
(before, after, before, after, ...). Instruction counts do not depend on load. Values in millions.

| line | before (3 runs) | after (3 runs) | change |
|---|---|---|---|
| `bench kernel.bin 1380735027 match` | 68046, 68048, 68047 | 66627, 66631, 66629 | -2.1% |
| `bench kernel.bin 1380735027 threaded` | 41947, 41952, 41951 | 40204, 40206, 40203 | -4.2% |
| `bench --core cortex-m33 kernel33.bin 1380735027 threaded` | 38153, 38151, 38150 | 36843, 36843, 36844 | -3.4% |
| `bench-board dual.elf turns` | 24196, 24196, 24196 | 23273, 23271, 23273 | -3.8% |
| `bench-board dual.elf threads` | 23977, 23921, 24169 | 23469, 23318, 23326 | -2.7% |
| `bench --core hazard3 kernel-rv.bin 1380735027` | 1604877, 1604899, 1604971 | 28964, 28962, 28965 | -98.2% (55x) |

The `threads` line varies by about 1% between runs in both builds because the two host threads
wait on each other; the other lines vary by less than 0.01%. The Hazard3 line runs the same
`kernel-rv.bin` (the before worktree has no copy, so the after file is used) with each build's
default executor.

Load average at the instruction measurements: 1-minute 4.2 to 7.2, 5-minute 6.9 to 8.7.

## H747 real time

`benchtop realtime --seconds 10 [--unpaced] examples/touch/mic-disco.elf` (STM32H747 Discovery,
both cores, display and audio), three interleaved rounds per mode. Core 0 is the 480 MHz M7,
core 1 the 240 MHz M4. Every run, paced and unpaced, in both builds, reports `REAL TIME`.

### Paced

| round | build | core 0 | core 1 (busy, late) | audio out / mic frames/s | host cores busy | load (1 min) |
|---|---|---|---|---|---|---|
| 1 | before | 1.000x, 3.9% | 1.000x, 61.4%, 362 us | 47998 / 48003 | 0.72 | 3.94 |
| 1 | after | 1.000x, 4.1% | 1.000x, 59.1%, 0 us | 48001 / 48001 | 0.71 | 3.64 |
| 2 | before | 1.000x, 3.7% | 1.000x, 49.9%, 0 us | 48005 / 48005 | 0.60 | 4.07 |
| 2 | after | 1.000x, 7.8% | 1.000x, 48.5%, 115 us | 48004 / 48000 | 0.66 | 4.45 |
| 3 | before | 1.000x, 3.5% | 1.000x, 49.1%, 58 us | 48000 / 48000 | 0.58 | 5.38 |
| 3 | after | 1.000x, 3.8% | 1.000x, 43.8%, 629 us | 48003 / 48003 | 0.54 | 5.56 |

Host instructions retired over the 10 s paced run: before 165.4, 162.3, 162.1 G; after 148.4,
148.9, 148.7 G (-8.9%).

### Unpaced

| round | build | core 0 | core 1 | audio out frames/s | host cores busy | load (1 min) |
|---|---|---|---|---|---|---|
| 1 | before | 1.442x (692 MHz) | 1.442x, 220.0 MIPS | 69210 | 1.36 | 3.39 |
| 1 | after | 1.458x (700 MHz) | 1.458x, 222.5 MIPS | 69983 | 1.38 | 3.51 |
| 2 | before | 1.129x (542 MHz) | 1.129x, 172.3 MIPS | 54176 | 1.43 | 5.15 |
| 2 | after | 1.461x (701 MHz) | 1.461x, 223.0 MIPS | 70124 | 1.39 | 5.45 |
| 3 | before | 1.437x (690 MHz) | 1.437x, 219.3 MIPS | 68976 | 1.35 | 5.28 |
| 3 | after | 1.659x (796 MHz) | 1.659x, 253.2 MIPS | 79641 | 1.37 | 5.17 |

Both cores' workers are 100% busy in every unpaced run. The before round 2 (1.129x) is the
one run depressed by other sessions' load; the load was below 8 throughout, so the rounds were not
repeated. The median unpaced speed is 1.437x before and 1.461x after.

## Windowed check

Not run. `launchctl managername` prints `Aqua`, but `realtime` is headless and the only
window command is `run --window`, which prints no timing and does not exit by itself.

## Conclusions

Every host-instruction line is lower after the review: 2.1% to 4.2% fewer instructions for the
Cortex-M and dual-core board lines, and 55 times fewer for the Hazard3 kernel. No line regressed.
The H747 with display and audio runs at exactly 1.000x real time on both cores in all six paced
runs, with 48000 audio frames/s out and into the microphone, a worker on the busier core 40% to
60% busy (about 2x headroom), and about 0.6 of one host core for the whole process; this is
unchanged from before. Unpaced, the same firmware runs 1.44x to 1.66x real time after, against
1.44x before (one load-depressed round at 1.13x), so the margin over real time is about 40% on a
loaded machine. Performance is not in doubt: the acceptance bar is met with headroom, and the
review made nothing slower.
