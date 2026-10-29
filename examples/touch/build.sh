#!/bin/sh
set -eu
cd "$(dirname "$0")"
transport=${1:-spi}
case "$transport" in
    joystick) feature_args="--features joystick"; output=joystick.elf ;;
    spi) feature_args=""; output=demo.elf ;;
    gpio) feature_args="--features gpio_spi"; output=gpio.elf ;;
    storage-spi) feature_args="--features h7_cores,dsi,audio,microphone,delay,storage"; output=storage-spi-disco.elf ;;
    storage) feature_args="--features h7_cores,dsi,audio,microphone,delay,storage,sdmmc"; output=storage-disco.elf ;;
    delay) feature_args="--features h7_cores,dsi,audio,microphone,delay"; output=delay-disco.elf ;;
    mic) feature_args="--features h7_cores,dsi,audio,microphone"; output=mic-disco.elf ;;
    busy) feature_args="--features h7_cores,dsi,audio,microphone,busy"; output=busy-disco.elf ;;
    audio) feature_args="--features h7_cores,dsi,audio"; output=audio-disco.elf ;;
    dsi) feature_args="--features h7_cores,dsi"; output=disco.elf ;;
    h7) feature_args="--features h7_cores"; output=h7.elf ;;
    full) feature_args="--features full_redraw"; output=full.elf ;;
    *) echo "usage: $0 [spi|gpio|full|h7|dsi|audio|mic|busy|delay|storage|storage-spi]" >&2; exit 2 ;;
esac
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
ARM_CC=${ARM_CC:-arm-none-eabi-gcc}
ARM_LD=${ARM_LD:-arm-none-eabi-ld}
target=thumb-freestanding
cpu=cortex-m33
fpu=fpv5-sp-d16
script=demo.ld
if [ "$transport" = storage-spi ] || [ "$transport" = storage ] || [ "$transport" = h7 ] || [ "$transport" = dsi ] || [ "$transport" = audio ] || [ "$transport" = mic ] || [ "$transport" = busy ] || [ "$transport" = delay ]; then
    target=thumbv7em-freestanding
    cpu=cortex-m4
    fpu=fpv4-sp-d16
    script=h7.ld
fi
if [ "$transport" = delay ] || [ "$transport" = storage ] || [ "$transport" = storage-spi ] || [ "$transport" = busy ]; then script=h7-delay.ld; fi
# The busy variant's double-precision kernel runs on the M7 only, so it is built for the M7's FPv5
# apart from the rest, which the M4 shares.
extra=""
if [ "$transport" = busy ]; then
    sysl build-c --target thumbv7em-freestanding-dp -O2 -DNDEBUG -o "$build_dir/m7-double.a" ../touch-m7-double
    extra="$build_dir/m7-double.a"
fi
libc=$($ARM_CC -mcpu="$cpu" -mthumb -mfpu="$fpu" -mfloat-abi=hard -print-file-name=libc.a)
libm=$($ARM_CC -mcpu="$cpu" -mthumb -mfpu="$fpu" -mfloat-abi=hard -print-file-name=libm.a)
libgcc=$($ARM_CC -mcpu="$cpu" -mthumb -mfpu="$fpu" -mfloat-abi=hard -print-libgcc-file-name)
include_root=$(dirname "$($ARM_CC -print-file-name=libc.a)")/../include
sysl build-c $feature_args --target "$target" -O2 --lib ../shared --include-path "$include_root" -DNDEBUG -o "$build_dir/demo.a" .
"$ARM_LD" -z noexecstack --gc-sections --build-id=none --no-warn-rwx-segments -L ../.. -T "$script" \
    --whole-archive "$build_dir/demo.a" --no-whole-archive $extra \
    --start-group "$libm" "$libc" "$libgcc" --end-group -o "$output"
