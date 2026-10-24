# bench — how fast the core can dispatch

`dispatch/` is the ARMv6-M interpreter run two ways over one guest: a `match` loop over decoded
records, and a chain of handlers joined by `become` (LLVM `musttail`). Both share the decoder, the
instruction semantics, the page-table memory path and the lazy N/Z flags; only the step from one
instruction to the next differs.

`guest/` is the workload: `kernel.c` (a bitwise CRC, a sieve, an insertion sort, a recursive
Fibonacci) built for the M0+. `kernel.bin` is committed because the tests embed it; rebuild it with

```
arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -O2 -ffreestanding -nostdlib -Wl,--no-warn-rwx-segments -T link.ld start.s kernel.c -o kernel.elf
arm-none-eabi-objcopy -O binary kernel.elf kernel.bin
```

Run it, with the answer the same C computes on the host for 1000 rounds:

```
cd dispatch
sysl build . -o dispatch
caffeinate -dimsu ./dispatch ../guest/kernel.bin 1380735027
sysl test .
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
/usr/bin/time -l ./dispatch ../guest/kernel.bin 1380735027 threaded
```

and divide `cycles elapsed` and `instructions retired` by three rounds of guest instructions. For
the `become` chain, sysl 0.5.5:

| | cycles / guest insn | host insns / guest insn |
|---|---|---|
| before `CMP`+`B<cond>` fusion, lazy V and the page-entry test | 3.83 | 16.0 |
| now | **3.38** | **13.7** |

The handler for `EORS` is ten AArch64 instructions with no frame, ending in `ldr x3, [x1, #0x10]!`
and `br x3`.
