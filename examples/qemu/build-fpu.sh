#!/bin/zsh
# Runs examples/virt/fpu.c on QEMU's Cortex-M4 (`mps2-an386`, FPv4-SP), Cortex-M33 (`mps2-an505`,
# FPv5-SP) and Cortex-M7 (`mps2-an500`, FPv5 with double precision), and writes what each prints to
# examples/virt/fpu-m4.qemu, fpu-m33.qemu and fpu-m7.qemu, the oracles the same program built for
# benchtop's boards is checked against: `zsh examples/qemu/build-fpu.sh`. Needs `qemu-system-arm`.
set -e
cd ${0:A:h}

flags=(-mthumb -mfloat-abi=softfp -O2 -ffreestanding -nostdlib -Wl,--no-warn-rwx-segments -DSEMIHOSTING)

oracle() {
    local machine=$1 script=$2 out=$3
    shift 3
    arm-none-eabi-gcc $flags $@ -T $script ../virt/fpu.c -o fpu-oracle.elf
    qemu-system-arm -M $machine -nographic -monitor none -serial none -chardev file,id=out,path=../virt/$out \
        -semihosting-config enable=on,target=native,chardev=out -kernel fpu-oracle.elf
    rm fpu-oracle.elf
}

oracle mps2-an386 an386.ld fpu-m4.qemu -mcpu=cortex-m4 -mfpu=fpv4-sp-d16
oracle mps2-an505 an505.ld fpu-m33.qemu -mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -DFPV5
oracle mps2-an500 an386.ld fpu-m7.qemu -mcpu=cortex-m7 -mfpu=fpv5-d16 -DFPV5 -DDOUBLE
