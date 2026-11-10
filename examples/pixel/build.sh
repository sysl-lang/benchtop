#!/bin/sh
# Rebuilds pixel.elf, the WS2812 chain's firmware (main.sysl), as examples/visual builds the workbench's.
set -eu
cd "$(dirname "$0")"
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
sysl build-c --target thumbv6m-freestanding -O2 --lib ../shared -o "$build_dir/pixel.a" .
ARM_LD=${ARM_LD:-arm-none-eabi-ld}
"$ARM_LD" --gc-sections --build-id=none --no-warn-rwx-segments -L ../.. -T ../visual/demo.ld \
    --whole-archive "$build_dir/pixel.a" --no-whole-archive -o pixel.elf
