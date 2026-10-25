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

Every implemented mnemonic also has a named instruction unit test and a row in
[the coverage matrix](../../sh/sysl/benchtop/riscv/COVERAGE.md). The independent Python
reference generator checks expected arithmetic and whole-machine state, using LLVM for
compressed instruction encodings. Boundary tests additionally check reserved encodings,
architectural HINTs, alignment, partial accesses and fetches at the ends of memory.

```sh
python3 examples/riscv/generate-instruction-tests.py         # regenerate from the repo root
python3 examples/riscv/generate-instruction-tests.py --check # verify fixtures without writing
sysl test . --filter "RV32 instruction"
```

These cover targeted architectural boundaries; they do not enumerate every possible
operand or machine-state combination. No claim is made for unimplemented instructions.
