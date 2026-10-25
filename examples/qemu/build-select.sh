#!/bin/zsh
# Differential oracle for Armv8.1-M conditional selects on QEMU's Cortex-M55.
set -e
cd ${0:A:h}
oracle_image=$(mktemp "${TMPDIR:-/tmp}/cortical-select.XXXXXX")
trap 'rm -f "$oracle_image"' EXIT
arm-none-eabi-gcc -march=armv8-m.main+nodsp -mthumb -mfloat-abi=soft -O2 -ffreestanding -nostdlib \
    -Wl,--no-warn-rwx-segments -DSEMIHOSTING -T an547.ld ../virt/select.c -o "$oracle_image"
qemu-system-arm -M mps3-an547 -nographic -monitor none -serial none \
    -chardev file,id=out,path=../virt/select.qemu \
    -semihosting-config enable=on,target=native,chardev=out -kernel "$oracle_image"
