#!/bin/sh
set -eu
cd "$(dirname "$0")"
transport=${1:-spi}
case "$transport" in
    spi) feature_args=""; output=demo.elf ;;
    gpio) feature_args="--features gpio_spi"; output=gpio.elf ;;
    full) feature_args="--features full_redraw"; output=full.elf ;;
    *) echo "usage: $0 [spi|gpio|full]" >&2; exit 2 ;;
esac
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
ARM_CC=${ARM_CC:-arm-none-eabi-gcc}
ARM_LD=${ARM_LD:-arm-none-eabi-ld}
libc=$($ARM_CC -mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard -print-file-name=libc.a)
libm=$($ARM_CC -mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard -print-file-name=libm.a)
libgcc=$($ARM_CC -mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard -print-libgcc-file-name)
include_root=$(dirname "$($ARM_CC -print-file-name=libc.a)")/../include
sysl build-c $feature_args --target thumb-freestanding -O2 --include-path "$include_root" -DNDEBUG -o "$build_dir/demo.a" .
"$ARM_LD" -z noexecstack --gc-sections --build-id=none --no-warn-rwx-segments -L ../.. -T demo.ld \
    --whole-archive "$build_dir/demo.a" --no-whole-archive \
    --start-group "$libm" "$libc" "$libgcc" --end-group -o "$output"
