#!/bin/sh
# Rebuilds rom.bin, benchtop's boot ROM for the Arm RP2350 board, from rom.S with arm-none-eabi.
set -eu
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
arm-none-eabi-as -mcpu=cortex-m33 -mthumb rom.S -o "$work/rom.o"
arm-none-eabi-ld -e 0 -Ttext=0 "$work/rom.o" -o "$work/rom.elf"
arm-none-eabi-objcopy -O binary "$work/rom.elf" rom.bin
