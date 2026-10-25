#!/bin/sh
set -eu
cd "$(dirname "$0")"
# Homebrew: brew install llvm lld. Override these for another LLVM installation.
CLANG=${CLANG:-/opt/homebrew/opt/llvm/bin/clang}
LD_LLD=${LD_LLD:-/opt/homebrew/opt/lld/bin/ld.lld}
OBJCOPY=${OBJCOPY:-/opt/homebrew/opt/llvm/bin/llvm-objcopy}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"$CLANG" -O2 -DHOST kernel.c -o "$work/host"
"$work/host" > kernel.out
for core in rv32i rv32im rv32ic rv32imc hazard3; do
    isa=$core
    if [ "$core" = hazard3 ]; then
        isa=rv32ima_zicsr_zifencei_zba_zbb_zbs_zbkb_zca_zcb_zcmp
    fi
    "$CLANG" --target=riscv32-unknown-elf -march="$isa" -mabi=ilp32 -mno-relax -O2 -ffreestanding -fno-builtin -c kernel.c -o "$work/kernel.o"
    "$CLANG" --target=riscv32-unknown-elf -march="$isa" -mabi=ilp32 -mno-relax -c start.S -o "$work/start.o"
    "$LD_LLD" --no-relax -T link.ld "$work/start.o" "$work/kernel.o" -o "$work/kernel.elf"
    "$OBJCOPY" -O binary "$work/kernel.elf" "kernel-$core.bin"
done

# The assembler is an independent encoding oracle for compressed expansion, including every
# immediate bit and both signs of jump/branch offsets. Keep the 2-byte/4-byte pairs unrelaxed.
"$CLANG" --target=riscv32-unknown-elf -march=rv32ic -mabi=ilp32 -mno-relax -c compressed.S -o "$work/compressed.o"
"$LD_LLD" --no-relax -T link.ld "$work/compressed.o" -o "$work/compressed.elf"
"$OBJCOPY" -O binary "$work/compressed.elf" compressed.bin
