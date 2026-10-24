#!/bin/zsh
# Builds the benchmark's guests: `zsh bench/guest/build.sh`.
#   kernel.bin    the one-core workload `cortical bench` runs from address 0, for the M0+
#   kernel33.bin  the same C for the M33 (`cortical bench --core cortex-m33`), without the DSP
#                 extension, which cortical's M33 leaves out
#   dual.elf      the two-core workload `cortical bench-board` runs on the virtual board, and
#   dual33.elf    the same for `virt33` (`cortical bench-board --board virt33`)
set -e
cd ${0:A:h}

arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -O2 -ffreestanding -nostdlib -Wl,--no-warn-rwx-segments \
    -T link.ld start.s kernel.c -o kernel.elf
arm-none-eabi-objcopy -O binary kernel.elf kernel.bin

arm-none-eabi-gcc -mcpu=cortex-m33+nodsp -mthumb -mfloat-abi=soft -O2 -ffreestanding -nostdlib \
    -Wl,--no-warn-rwx-segments -T link.ld start.s kernel.c -o kernel33.elf
arm-none-eabi-objcopy -O binary kernel33.elf kernel33.bin

virt=../../examples/virt

arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -O2 -ffreestanding -nostdlib -Wl,--no-warn-rwx-segments \
    -I $virt -T $virt/virt.ld $virt/start.c dual.c -lgcc -o dual.elf
arm-none-eabi-gcc -mcpu=cortex-m33+nodsp -mthumb -mfloat-abi=soft -O2 -ffreestanding -nostdlib \
    -Wl,--no-warn-rwx-segments -I $virt -T $virt/virt.ld $virt/start.c dual.c -lgcc -o dual33.elf
