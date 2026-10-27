# Instruction timing estimates

Cortex-M core descriptions accept `instruction-timing = estimated` or `unit`.
Unit timing is the default and preserves one cycle per retired instruction.
The Discovery demo explicitly selects estimated timing for both cores.
Reset is required when changing modes. Instruction counts remain instruction
counts; pacing, SysTick, sleeping and device deadlines use the cycle clock.

Each CPU builds a 256-entry operation cost table on reset. Costs depend on its
core model, with a separate FP operation/precision cost function. The common
one-cycle ALU handlers retain their existing instruction increment. Only
potentially multi-cycle handlers add extra cycles, and the decoded instruction
record remains 16 bytes. Fused compare/branch handlers charge both instructions
exactly once. Faulting memory instructions and debugger breakpoints do not
retire or charge cycles. Skipped IT instructions consume one cycle.

The `unit` option bypasses extra charging. `CYCLE_ACCOUNTING` in
`v6m/cycles.sysl` can also disable accounting at compile time while retaining the
implementation. The benchmark accepts `--timing unit|estimated` after `--core`:

```
./benchtop bench --core cortex-m7 --timing estimated bench/guest/kernel33.bin 1380735027 threaded
```

## Sources and chosen constants

The primary baseline is Arm's [Cortex-M4 TRM, sections 3.3 and 7.2.3](https://documentation-service.arm.com/static/5fce431be167456a35b36ade),
with [Cortex-M0+ instruction timing](https://developer.arm.com/documentation/ddi0484/b/Programmers-Model/Instruction-set-summary)
and [Cortex-M7 architecture](https://documentation-service.arm.com/static/5e906dc68259fe2368e2abbe).
These are **serial instruction estimates**, not pipeline or cache simulation.

Integer ALU operations default to one cycle; single loads/stores use two,
doubleword transfers three. Division uses seven as a constant representative
of the M4's published operand-dependent range. Conditional branches use two;
unconditional branches use three except M0+ and M7, where the estimate is two.
Special-register accesses use the baseline/mainline differences in the table.
Multiple transfers use a fixed five cycles, assuming four registers, and
returns through a multiple load use seven. They deliberately do not inspect
register-list lengths or operands during execution.

M4 single-precision FP division/square root use fourteen; multiply-accumulate
and fused operations use three. Ordinary FP arithmetic uses one. For M7 double
precision the initial estimates are twenty-eight for division/square root,
five for accumulate and three for ordinary arithmetic. These double-precision
constants are provisional estimates, not a published M7 cycle table.

M7 estimates borrow the M4 scalar baseline where a portable serial cost is
unavailable; they do not model dual issue or overlapping FP execution. Other
catalog Cortex-M models currently share this scalar baseline, with model
specific branch, system-register and long-multiply choices. Their tables should
be calibrated further before using them to predict workload execution time.
No cache misses, flash wait states, bus contention, exception stacking latency,
FP denormal assists or DSP pipeline dependencies are added. RISC-V's standalone
interpreter is unchanged by this Cortex-M timing implementation.
