#!/bin/zsh
# Builds the benchmark's guests: `zsh bench/guest/build.sh`.
#   kernel.bin  the one-core workload `cortical bench` runs from address 0
#   dual.elf    the two-core workload `cortical bench-board` runs on the virtual board
set -e
cd ${0:A:h}

arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -O2 -ffreestanding -nostdlib -Wl,--no-warn-rwx-segments \
    -T link.ld start.s kernel.c -o kernel.elf
arm-none-eabi-objcopy -O binary kernel.elf kernel.bin

virt=../../examples/virt

arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -O2 -ffreestanding -nostdlib -Wl,--no-warn-rwx-segments \
    -I $virt -T $virt/virt.ld $virt/start.c dual.c -lgcc -o dual.elf
