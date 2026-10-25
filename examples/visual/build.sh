#!/bin/sh
set -eu
cd "$(dirname "$0")"
CC=${CC:-arm-none-eabi-gcc}
"$CC" -mcpu=cortex-m0plus -mthumb -Os -ffreestanding -fno-builtin -nostdlib -Wl,--build-id=none,--no-warn-rwx-segments -T demo.ld demo.c -o demo.elf
