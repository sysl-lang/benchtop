#!/bin/zsh
# Refresh the integer DSP oracle from QEMU's Cortex-M33.
set -e
cd ${0:A:h}
oracle_image=$(mktemp "${TMPDIR:-/tmp}/benchtop-dsp.XXXXXX")
trap 'rm -f "$oracle_image"' EXIT
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -mfloat-abi=soft -O2 -ffreestanding -nostdlib \
    -Wl,--no-warn-rwx-segments -DSEMIHOSTING -T an505.ld ../virt/dsp.c -o "$oracle_image"
qemu-system-arm -M mps2-an505 -nographic -monitor none -serial none \
    -chardev file,id=out,path=../virt/dsp.qemu \
    -semihosting-config enable=on,target=native,chardev=out -kernel "$oracle_image"
