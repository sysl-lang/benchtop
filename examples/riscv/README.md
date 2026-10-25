# RISC-V fixtures

`sh build.sh` rebuilds the committed raw images and `kernel.out`, the answer computed by the
same C compiled for the host. LLVM clang, lld and llvm-objcopy are required; override their
paths with `CLANG`, `LD_LLD` and `OBJCOPY` (the defaults are Homebrew's).

`kernel.c` exercises arithmetic, unsigned comparisons, loops, stack loads/stores and calls.
`start.S` sets SP to the top of the bench's 1 MB RAM, calls `kernel`, and stops at EBREAK.
Each ISA is compiled separately, so compressed guests execute 32-bit instructions at halfword
boundaries. Unit tests also exercise M operations, including all division edge cases.

`compressed.S` contains 118 compressed instructions paired with their canonical RV32I forms.
Assembling and linking without relaxation gives 6 bytes per pair in `compressed.bin`, an
independent oracle for every RV32C integer instruction family and all immediate fields.

Run from the repository root:

```sh
sysl build . -o benchtop
./benchtop bench --core rv32imc examples/riscv/kernel-rv32imc.bin 3132271724
./benchtop check --chip examples/riscv/chip.hocon
sysl test . --filter RV32
```

`chip.hocon` demonstrates core names in the shared description format. It is not yet a
runnable board; neither the bare ISA profiles nor the interpreter implement an interrupt
controller or RP2350 devices.
