#!/bin/zsh
# Runs examples/virt/isa.c on QEMU's Cortex-M33 (`mps2-an505`) and writes what it prints to
# examples/virt/isa.qemu, the oracle the same program built for `virt33` is checked against:
# `zsh examples/qemu/build.sh`. Needs `qemu-system-arm`.
set -e
cd ${0:A:h}

arm-none-eabi-gcc -mcpu=cortex-m33+nodsp -mthumb -mfloat-abi=soft -O2 -ffreestanding -nostdlib \
    -Wl,--no-warn-rwx-segments -DSEMIHOSTING -T an505.ld ../virt/isa.c -o isa-an505.elf

qemu-system-arm -M mps2-an505 -nographic -monitor none -serial none -chardev file,id=out,path=../virt/isa.qemu \
    -semihosting-config enable=on,target=native,chardev=out -kernel isa-an505.elf
rm isa-an505.elf
