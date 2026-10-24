#!/bin/zsh
# Builds the virtual board's example programs: `zsh build.sh`, then `cortical run echo.elf`.
# Each is built for the M0+ board virt, the M4/M7 boards virt4/virt7, and the M33 board virt33.
# The Thumb-2 builds stay within cortical's integer subsets, without DSP or floating point.
set -e
cd ${0:A:h}

m0=(-mcpu=cortex-m0plus -mthumb)
# The implemented M4 subset omits DSP; Cortex-M3 code uses the shared ARMv7-M ISA.
m4=(-mcpu=cortex-m3 -mthumb -mfloat-abi=soft)
# Keep the shared ISA while tuning instruction scheduling for M7.
m7=(-mcpu=cortex-m3 -mtune=cortex-m7 -mthumb -mfloat-abi=soft)
m33=(-mcpu=cortex-m33+nodsp -mthumb -mfloat-abi=soft)
flags=(-O2 -ffreestanding -nostdlib -Wl,--no-warn-rwx-segments -T virt.ld)

for program in echo irq dual count pingpong order crunch; do
    arm-none-eabi-gcc $m0 $flags start.c $program.c -lgcc -o $program.elf
    arm-none-eabi-gcc $m33 $flags start.c $program.c -lgcc -o $program-m33.elf
    arm-none-eabi-gcc $m4 $flags start.c $program.c -lgcc -o $program-m4.elf
    arm-none-eabi-gcc $m7 $flags start.c $program.c -lgcc -o $program-m7.elf
done

# isa.c is for the M33 alone; examples/qemu/build.sh runs it on QEMU for isa.qemu.
arm-none-eabi-gcc $m33 $flags start.c isa.c -lgcc -o isa-m33.elf
arm-none-eabi-gcc $m7 $flags start.c isa.c -lgcc -o isa-m7.elf

# exclusive.c is for Thumb-2 cores: an M0+ has no exclusives.
arm-none-eabi-gcc $m33 $flags start.c exclusive.c -lgcc -o exclusive-m33.elf
arm-none-eabi-gcc $m4 $flags start.c exclusive.c -lgcc -o exclusive-m4.elf
arm-none-eabi-gcc $m7 $flags start.c exclusive.c -lgcc -o exclusive-m7.elf

# irq.c for moved.hocon, whose console is at 0x50000000 and raises IRQ 7:
# `cortical run --board moved.hocon irq-moved.elf`.
arm-none-eabi-gcc $m0 $flags -DCONSOLE_BASE=0x50000000 -DCONSOLE_IRQ=7 start.c irq.c -lgcc -o irq-moved.elf

# What crunch.c prints built for the host, which both builds must print too.
cc -DHOST -O2 crunch.c -o crunch-host
./crunch-host > crunch.out
rm crunch-host
