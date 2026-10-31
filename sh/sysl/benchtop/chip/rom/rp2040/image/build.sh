#!/bin/sh
# Builds benchtop's own RP2040 boot ROM into ../rom.bin, the 16 KB the `pico` board maps at 0
# (chip/rp2040.sysl embeds it). sysl compiles the image and the package's routines for the Cortex-M0+
# into an archive; Arm's GNU linker places it with rom.ld; objcopy writes the binary. Needs sysl (the
# version sysl.hocon names) and Arm's arm-none-eabi toolchain. The build is reproducible: the same
# sources give the same bytes, whose SHA-256 the script prints.
set -eu
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# -Oz: at -Os the compiler unrolls the division and square-root loops, and the image does not fit.
sysl build-c . --target thumbv6m-freestanding -Oz -o "$work/librom.a"
arm-none-eabi-ld -T rom.ld --gc-sections -nostdlib -Map "$work/rom.map" -o "$work/rom.elf" \
    -u rom_reset -u rom_table_lookup "$work/librom.a"
arm-none-eabi-objcopy -O binary --gap-fill 0 "$work/rom.elf" ../rom.bin
size=$(wc -c < ../rom.bin | tr -d ' ')
if [ "$size" -ne 16384 ]; then
    echo "rom.bin is $size bytes, not 16384" >&2
    exit 1
fi
echo "rom.bin: 16384 bytes, the code and data ending at 0x$(arm-none-eabi-nm "$work/rom.elf" | awk '$3 == "rom_code_end" { print $1 }')"
shasum -a 256 ../rom.bin
if [ "${KEEP_ELF:-}" != "" ]; then cp "$work/rom.elf" "$work/rom.map" "$KEEP_ELF"/; fi
