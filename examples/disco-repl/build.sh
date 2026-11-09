#!/bin/sh
set -eu
cd "$(dirname "$0")"
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
ARM_LD=${ARM_LD:-arm-none-eabi-ld}
sysl build-c --target thumbv7em-freestanding -O2 --lib ../shared -DNDEBUG -o "$build_dir/repl.a" .
# The firmware is hard-float (VFPv4-D16, single precision), so the matching
# newlib multilib is thumb/v7e-m+fp/hard; forth's float words need its libm.
ARM_GCC=${ARM_GCC:-arm-none-eabi-gcc}
gcc_flags="-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16"
libm_dir=$(dirname "$($ARM_GCC $gcc_flags -print-file-name=libm.a)")
libgcc_dir=$(dirname "$($ARM_GCC $gcc_flags -print-libgcc-file-name)")
"$ARM_LD" --gc-sections --build-id=none --no-warn-rwx-segments -T repl.ld \
    --whole-archive "$build_dir/repl.a" --no-whole-archive \
    -L"$libm_dir" -L"$libgcc_dir" --start-group -lm -lc -lgcc --end-group \
    -o repl-disco.elf
