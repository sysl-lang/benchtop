#!/bin/sh
set -eu
cd "$(dirname "$0")"
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
ARM_LD=${ARM_LD:-arm-none-eabi-ld}
sysl build-c --target thumbv7em-freestanding -O2 --lib ../shared -DNDEBUG -o "$build_dir/repl.a" .
"$ARM_LD" --gc-sections --build-id=none --no-warn-rwx-segments -T repl.ld \
    --whole-archive "$build_dir/repl.a" --no-whole-archive -o repl-disco.elf
