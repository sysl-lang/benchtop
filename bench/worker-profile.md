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


## Independent per-core wall pacing

Normal paced execution now uses `timing.CorePacer`, an ISA-independent clock
policy. Each instance receives its own frequency, host epoch and guest-clock
origin. It checks host time after a bounded batch (up to one million ticks,
limited to about one millisecond at clocks of at least 1 kHz). A faster core
waits against its own timestamp; pause/resume rebases both domains without
charging paused host time to guest execution.

The old short coordinator jobs remain for unpaced manual budgets and explicit
deterministic execution. Normal paced jobs use a longer control window while
each worker paces and publishes itself independently. Pause interrupts both
host-time waits and guest-ordering waits; reset and exact instruction stepping
still operate on a stopped pair. Sleeping cores also reach pacing checkpoints,
so WFI cannot jump an entire future control window into a UI snapshot.

The guest-time ordering limit is distinct and remains conservative. Independent
wall pacing is not a claim that all cross-core synchronization has been removed.
The pacing policy contains no M4/M7 names or ISA branches. Regression tests cover
matching M0+ cores, M33/M0+, M4/M7 and M7/M4, including unequal rates; the clock
math also covers arbitrary frequencies, rounding, long epochs and pause rebasing.


A subsequent 19-interval live run with independent pacing measured a median of
approximately 293/146 million instructions per second (M7/M4), with a 120 FPS
host UI. This does not establish an improvement over the earlier runs: host
load changed, and the conservative guest-time ordering limit still couples
shared execution. The change separates per-core wall-clock pacing and
publication from that ordering policy; further ordering/peripheral optimization
is required before claiming sustained full-clock emulation for this workload.


## Real-time cores and DSI now execute independently

Normal paced execution no longer applies the global peer-clock lead limit or
periodic shared job horizons. Each CPU runs until a control request or guest
stop and paces against its own host-time epoch. Sleeping CPUs use their own
pacing/event deadline. A delayed CPU reports `late_us` in observations and the
optional live trace; the desktop warns above 5 ms without masking an existing
diagnostic. The ISA-independent policy applies to matching and heterogeneous
core models. Explicit deterministic and unpaced manual jobs retain coordinated
virtual-time behavior for repeatable tests.

This intentionally trades cycle-level cross-core timing fidelity for real-time
functional emulation. Host atomic shared-memory accesses, firmware barriers,
FIFO ordering and shared peripheral locks still apply; firmware must synchronize
shared data as it would on hardware. One core's instruction count no longer
limits its peer. A regression blocks a core in a host device callback and
requires its peer to continue retiring instructions while the blocked core
reports lateness.

DSI display interpretation now has its own persistent host worker, with an
existing `Channel[DsiPacket]` providing bounded semaphore-style handoff. CPU
producers use nonblocking queue submission under the small register lock. The
device worker releases the channel lock before taking the display lock or
processing pixels. A full host queue applies command-busy backpressure; it does
not block unrelated CPU instruction execution or discard accepted packets.
Readback is deferred and ordered, with request IDs rejecting stale replies after
cancellation. Pause/reset/step/shutdown barriers drain completed packets and
catch up any command whose queue backpressure has cleared at the parked time.

A second regression deliberately holds the UI display lock while DSI work is
pending, and requires both CPU workers to progress on threads distinct from the
DSI worker. Further tests cover bounded queue saturation, consume-once behavior,
write/read ordering and worker persistence across reset. The previous throughput
numbers describe earlier scheduler versions and must be remeasured for this one.
