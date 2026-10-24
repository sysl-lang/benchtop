#!/bin/zsh
# Builds the virtual board's example programs: `zsh build.sh`, then `cortical run echo.elf`.
set -e
cd ${0:A:h}

for program in echo irq dual count pingpong order; do
    arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -O2 -ffreestanding -nostdlib \
        -Wl,--no-warn-rwx-segments -T virt.ld start.c $program.c -lgcc -o $program.elf
done

# irq.c for moved.hocon, whose console is at 0x50000000 and raises IRQ 7:
# `cortical run --board moved.hocon irq-moved.elf`.
arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -O2 -ffreestanding -nostdlib \
    -DCONSOLE_BASE=0x50000000 -DCONSOLE_IRQ=7 \
    -Wl,--no-warn-rwx-segments -T virt.ld start.c irq.c -lgcc -o irq-moved.elf
