#!/bin/sh
# Assembles rp2040-standin.S, benchtop's stand-in RP2040 boot ROM, into rp2040-standin.bin, which the
# chip layer embeds (chip/bootrom40.sysl). Needs Arm's arm-none-eabi toolchain.
set -eu
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
arm-none-eabi-as -mcpu=cortex-m0plus -mthumb -o "$work/standin.o" rp2040-standin.S
arm-none-eabi-ld -Ttext=0 -e _start -o "$work/standin.elf" "$work/standin.o"
arm-none-eabi-objcopy -O binary "$work/standin.elf" rp2040-standin.bin
