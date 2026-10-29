#!/bin/sh
# Rebuilds blink.elf, the RISC-V Pico 2 example, with the LLVM toolchain the RISC-V fixtures use
# (examples/riscv/build.sh). Override CLANG and LD_LLD for another LLVM installation.
set -eu
cd "$(dirname "$0")"
CLANG=${CLANG:-/opt/homebrew/opt/llvm/bin/clang}
LD_LLD=${LD_LLD:-/opt/homebrew/opt/lld/bin/ld.lld}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
isa=rv32ima_zicsr_zifencei_zba_zbb_zbs_zbkb_zca_zcb_zcmp
"$CLANG" --target=riscv32-unknown-elf -march="$isa" -mabi=ilp32 -mno-relax -O2 -ffreestanding -fno-builtin -c blink.c -o "$work/blink.o"
"$CLANG" --target=riscv32-unknown-elf -march="$isa" -mabi=ilp32 -mno-relax -c start.S -o "$work/start.o"
"$LD_LLD" --no-relax -T link.ld "$work/start.o" "$work/blink.o" -o blink.elf
