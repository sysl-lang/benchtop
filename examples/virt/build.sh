#!/bin/zsh
# Builds the virtual board's example programs: `zsh build.sh`, then `cortical run echo.elf`.
# Each is built twice: `<program>.elf` for the M0+ board `virt`, and `<program>-m33.elf` for the
# M33 board `virt33` (`cortical run --board virt33 echo-m33.elf`), without the DSP extension, which
# cortical's M33 leaves out.
set -e
cd ${0:A:h}

m0=(-mcpu=cortex-m0plus -mthumb)
m33=(-mcpu=cortex-m33+nodsp -mthumb -mfloat-abi=soft)
flags=(-O2 -ffreestanding -nostdlib -Wl,--no-warn-rwx-segments -T virt.ld)

for program in echo irq dual count pingpong order crunch; do
    arm-none-eabi-gcc $m0 $flags start.c $program.c -lgcc -o $program.elf
    arm-none-eabi-gcc $m33 $flags start.c $program.c -lgcc -o $program-m33.elf
done

# exclusive.c is for the M33 alone: an M0+ has no exclusives.
arm-none-eabi-gcc $m33 $flags start.c exclusive.c -lgcc -o exclusive-m33.elf

# irq.c for moved.hocon, whose console is at 0x50000000 and raises IRQ 7:
# `cortical run --board moved.hocon irq-moved.elf`.
arm-none-eabi-gcc $m0 $flags -DCONSOLE_BASE=0x50000000 -DCONSOLE_IRQ=7 start.c irq.c -lgcc -o irq-moved.elf

# What crunch.c prints built for the host, which both builds must print too.
cc -DHOST -O2 crunch.c -o crunch-host
./crunch-host > crunch.out
rm crunch-host
