#!/bin/sh
# Rebuilds the Pico (RP2040) examples (README.md lists them) with the Pico SDK and Arm's arm-none-eabi
# toolchain. PICO_SDK_PATH defaults to the org's SDK checkout (README.md names its commit).
set -eu
cd "$(dirname "$0")"
export PICO_SDK_PATH=${PICO_SDK_PATH:-$HOME/dev/sysl-lang/pico-scratch/pico-sdk}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cmake -S . -B "$work" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$work"
for prog in hello_serial blink hello_multicore; do
    cp "$work/$prog.elf" "$prog.elf"
done
