# Discovery CPU worker profile

Measured October 26, 2026 on an Apple M5 Pro. Firmware:
`examples/touch/disco.elf`; board: `stm32h747-disco`. M7 runs its placeholder
NOP loop; M4 polls FT6206 over I2C4. This is not an audio/DSP benchmark.

## Method

Run the normal windowed demo with `BENCHTOP_CPU_TRACE=1`. Measure approximately
one-second published retired-instruction deltas. For stack attribution, use
macOS `/usr/bin/sample <pid> 5 1 -file <output>` on that process. Native sampling
perturbs absolute throughput substantially, so use intervals outside the
sampling window for the speed comparison.

The table classifies exclusive stack samples by ancestor: interpreter execution,
board MMIO callbacks, device polling/lock waits, or scheduler/worker coordination.
Inlining limits attribution within the scheduler category. These are sampled
wall-time proportions, including blocked time, not hardware CPU cycle counts.

## Before the DSI fix

2724 samples per worker:

| Category | M7 | M4 |
|---|---:|---:|
| CPU execution, including system/FP state handling | 15.4% | 23.1% |
| Guest MMIO/device callbacks | 0% | 27.5% |
| Waiting inside DSI signal polling | 34.8% | 29.8% |
| Waiting inside device-deadline polling | 3.9% | 6.4% |
| Other device checks and lock wakeups | 7.3% | 4.6% |
| Scheduler and worker coordination | 38.6% | 8.7% |

The UI held `display_lock` while copying and expanding all 384000 display
pixels for each host frame. Both CPU workers took the same lock from
`signal_dsi` and `dsi_due` every guest quantum, including when DSI was idle.
The main UI thread spent approximately 34% of its samples in `tft_frame`.

## Fix and verification

DSI now publishes its next completion deadline atomically. An idle or not-yet-due
signal check needs no display lock. Completion, MMIO and display state still use
the existing lock, with the deadline published after every mutation and reset.
Guest instruction counts, clock ratios, packet duration calculations and the allowed
inter-core clock skew are unchanged.

A concurrency regression holds the UI display lock while both guest workers run
busy loops. Both must finish their budget before that lock is released. Existing
DSI packet timing, cancellation, sleeping transfers and guest-rendered touch
interaction tests also pass.

## After the idle-DSI fix

2517 samples per worker:

| Category | M7 | M4 |
|---|---:|---:|
| CPU execution, including system/FP state handling | 14.2% | 29.9% |
| Guest MMIO/device callbacks | 0% | 35.4% |
| Waiting inside DSI signal polling | 0% | 0% |
| Other device checks and lock wakeups | 2.0% | 0.1% |
| Scheduler and worker coordination | 83.8% | 34.6% |

Device-deadline lock wait rounded to 0% on both workers. These proportions show
that the removed display wait largely became peer/core coordination time, not
additional useful execution. The M4's I2C polling and tight synchronization now
limit the board. The scheduler category includes spin waiting, condition waits,
publishing clocks, lag checks and bounded worker-batch barriers; it is not all
spent asleep.

Outside the native sampling interval, median rates were approximately
289/144 million instructions per second before, and 307/153 million after the
idle-DSI fix: about a 6% increase in that pair of runs. The host UI stayed around 120 FPS. This does
not establish full 480/240 target throughput, exact physical H747 timing or
real-time audio performance.

An additional I2C callback borrowing experiment produced slower end-to-end
samples in this session and was not retained. The existing callback dispatch
remains; absent ADCs now avoid taking an unnecessary scheduler lock.

## Reproducing

```sh
sysl build .
BENCHTOP_CPU_TRACE=1 ./benchtop run --window --board stm32h747-disco \
    --frames 2400 examples/touch/disco.elf > target/disco-rates.log
# From another terminal, sample only this worktree's running benchtop PID:
sample <pid> 5 1 -file target/disco-worker-sample.txt
```

Diagnostic artifacts from this session are under `target/`: the before and after
stack samples, the bounded live-rate logs, and the full regression-suite log.
They are intentionally not committed because they contain local process data
and are specific to this host load and build.


## Host-load limitation

At 18:57 local time, `uptime` reported load averages of 51.69 / 45.96 / 31.28
on this 18-CPU host. The final 693-test suite passed, but took 197 seconds,
compared with 75--103 seconds in earlier runs. Absolute speed measurements and
small before/after differences are therefore noisy; the 6% observation is not
a controlled maximum-throughput result. The native profiles and concurrency
regression establish that the idle DSI/UI wait is removed independently of that
speed comparison. A quieter-host measurement is still needed for sustained
480/240 target-rate claims.
