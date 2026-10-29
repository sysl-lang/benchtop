#!/bin/zsh
# Builds the benchmark's guests: `zsh bench/guest/build.sh`.
#   kernel.bin    the one-core workload `benchtop bench` runs from address 0, for the M0+
#   kernel33.bin  the same C for the M33 (`benchtop bench --core cortex-m33`), without the DSP
#                 extension, which benchtop's M33 leaves out
#   it33.bin      a loop that is mostly IT blocks (it.s; it.c computes its answer), for the M33
#   dual.elf      the two-core workload `benchtop bench-board` runs on the virtual board, and
#   dual33.elf    the same for `virt33` (`benchtop bench-board --board virt33`)
#   kernel-rv.bin the one-core workload for the Hazard3 RISC-V core
set -e
cd ${0:A:h}

arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -O2 -ffreestanding -nostdlib -Wl,--no-warn-rwx-segments \
    -T link.ld start.s kernel.c -o kernel.elf
arm-none-eabi-objcopy -O binary kernel.elf kernel.bin

arm-none-eabi-gcc -mcpu=cortex-m33+nodsp -mthumb -mfloat-abi=soft -O2 -ffreestanding -nostdlib \
    -Wl,--no-warn-rwx-segments -T link.ld start.s kernel.c -o kernel33.elf
arm-none-eabi-objcopy -O binary kernel33.elf kernel33.bin

arm-none-eabi-gcc -mcpu=cortex-m33+nodsp -mthumb -mfloat-abi=soft -ffreestanding -nostdlib \
    -Wl,--no-warn-rwx-segments -T link.ld start.s it.s -o it33.elf
arm-none-eabi-objcopy -O binary it33.elf it33.bin

virt=../../examples/virt

arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -O2 -ffreestanding -nostdlib -Wl,--no-warn-rwx-segments \
    -I $virt -T $virt/virt.ld $virt/start.c dual.c -lgcc -o dual.elf
arm-none-eabi-gcc -mcpu=cortex-m33+nodsp -mthumb -mfloat-abi=soft -O2 -ffreestanding -nostdlib \
    -Wl,--no-warn-rwx-segments -I $virt -T $virt/virt.ld $virt/start.c dual.c -lgcc -o dual33.elf

# kernel-rv.bin: the same C for the RP2350's Hazard3 (`benchtop bench --core hazard3`), with
# Homebrew's LLVM (override CLANG, LD_LLD and OBJCOPY for another).
clang=${CLANG:-/opt/homebrew/opt/llvm/bin/clang}
lld=${LD_LLD:-/opt/homebrew/opt/lld/bin/ld.lld}
objcopy=${OBJCOPY:-/opt/homebrew/opt/llvm/bin/llvm-objcopy}
rv=(--target=riscv32-unknown-elf -march=rv32ima_zicsr_zifencei_zba_zbb_zbs_zbkb_zca_zcb_zcmp -mabi=ilp32 -mno-relax)
work=$(mktemp -d)
$clang $rv -O2 -ffreestanding -fno-builtin -c kernel.c -o $work/kernel.o
$clang $rv -c start-rv.S -o $work/start.o
$lld --no-relax -T ../../examples/riscv/link.ld $work/start.o $work/kernel.o -o $work/kernel.elf
$objcopy -O binary $work/kernel.elf kernel-rv.bin
rm -rf $work
