#!/bin/zsh
# Builds the virtual board's example programs: `zsh build.sh`, then `cortical run echo.elf`.
set -e
cd ${0:A:h}

for program in echo irq; do
    arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -O2 -ffreestanding -nostdlib \
        -Wl,--no-warn-rwx-segments -T virt.ld start.c $program.c -lgcc -o $program.elf
done
