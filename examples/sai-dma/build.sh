#!/bin/sh
set -eu
cd "$(dirname "$0")"
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
sysl build-c --target thumbv7em-freestanding -O2 --lib ../shared -o "$build_dir/probe.a" .
ARM_LD=${ARM_LD:-arm-none-eabi-ld}
"$ARM_LD" --gc-sections --build-id=none --no-warn-rwx-segments -T demo.ld \
    --whole-archive "$build_dir/probe.a" --no-whole-archive -o demo.elf
