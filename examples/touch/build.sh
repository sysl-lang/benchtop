#!/bin/sh
set -eu
cd "$(dirname "$0")"
transport=${1:-spi}
case "$transport" in
    spi) feature_args=""; output=demo.elf ;;
    gpio) feature_args="--features gpio_spi"; output=gpio.elf ;;
    h7) feature_args="--features h7_cores"; output=h7.elf ;;
    full) feature_args="--features full_redraw"; output=full.elf ;;
    *) echo "usage: $0 [spi|gpio|full|h7]" >&2; exit 2 ;;
esac
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
ARM_CC=${ARM_CC:-arm-none-eabi-gcc}
ARM_LD=${ARM_LD:-arm-none-eabi-ld}
target=thumb-freestanding
cpu=cortex-m33
fpu=fpv5-sp-d16
script=demo.ld
if [ "$transport" = h7 ]; then
    target=thumbv7em-freestanding
    cpu=cortex-m4
    fpu=fpv4-sp-d16
    script=h7.ld
fi
libc=$($ARM_CC -mcpu="$cpu" -mthumb -mfpu="$fpu" -mfloat-abi=hard -print-file-name=libc.a)
libm=$($ARM_CC -mcpu="$cpu" -mthumb -mfpu="$fpu" -mfloat-abi=hard -print-file-name=libm.a)
libgcc=$($ARM_CC -mcpu="$cpu" -mthumb -mfpu="$fpu" -mfloat-abi=hard -print-libgcc-file-name)
include_root=$(dirname "$($ARM_CC -print-file-name=libc.a)")/../include
sysl build-c $feature_args --target "$target" -O2 --include-path "$include_root" -DNDEBUG -o "$build_dir/demo.a" .
"$ARM_LD" -z noexecstack --gc-sections --build-id=none --no-warn-rwx-segments -L ../.. -T "$script" \
    --whole-archive "$build_dir/demo.a" --no-whole-archive \
    --start-group "$libm" "$libc" "$libgcc" --end-group -o "$output"
