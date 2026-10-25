#!/bin/zsh
# Builds the virtual board's example programs: `zsh build.sh`, then `cortical run echo.elf`.
# Each is built for the M0+ board virt, the M4/M7 boards virt4/virt7, and the M33 board virt33.
# The Thumb-2 builds stay within cortical's integer instruction sets, without floating point; the
# M4 and M7 builds use the DSP extension those cores always have.
set -e
cd ${0:A:h}

m0=(-mcpu=cortex-m0plus -mthumb)
m4=(-mcpu=cortex-m4 -mthumb -mfloat-abi=soft)
m7=(-mcpu=cortex-m7 -mthumb -mfloat-abi=soft)
m33=(-mcpu=cortex-m33+nodsp -mthumb -mfloat-abi=soft)
flags=(-O2 -ffreestanding -nostdlib -Wl,--no-warn-rwx-segments -T virt.ld)

for program in echo irq dual count pingpong order crunch; do
    arm-none-eabi-gcc $m0 $flags start.c $program.c -lgcc -o $program.elf
    arm-none-eabi-gcc $m33 $flags start.c $program.c -lgcc -o $program-m33.elf
    arm-none-eabi-gcc $m4 $flags start.c $program.c -lgcc -o $program-m4.elf
    arm-none-eabi-gcc $m7 $flags start.c $program.c -lgcc -o $program-m7.elf
done

# isa.c is for the M33 alone; examples/qemu/build.sh runs it on QEMU for isa.qemu.
arm-none-eabi-gcc $m33 $flags start.c isa.c -lgcc -o isa-m33.elf
arm-none-eabi-gcc $m7 $flags start.c isa.c -lgcc -o isa-m7.elf

# exclusive.c is for Thumb-2 cores: an M0+ has no exclusives.
arm-none-eabi-gcc $m33 $flags start.c exclusive.c -lgcc -o exclusive-m33.elf
arm-none-eabi-gcc $m4 $flags start.c exclusive.c -lgcc -o exclusive-m4.elf
arm-none-eabi-gcc $m7 $flags start.c exclusive.c -lgcc -o exclusive-m7.elf

# Remaining scalar models. Armv8.1-M guests use the shared v8-M ISA here; select.c separately
# exercises the new v8.1-M conditional-select instructions without relying on unsupported loops/MVE.
for target in m0 m1 m3 m23 m35p m52 m55 m85; do
    case $target in
        m0|m1) scalar=(-mcpu=cortex-m0 -mthumb) ;;
        m3) scalar=(-mcpu=cortex-m3 -mthumb -mfloat-abi=soft) ;;
        m23) scalar=(-mcpu=cortex-m23 -mthumb -mfloat-abi=soft) ;;
        m35p) scalar=(-march=armv8-m.main+nodsp -mthumb -mfloat-abi=soft) ;;
        *) scalar=(-march=armv8-m.main+nodsp -mtune=cortex-$target -mthumb -mfloat-abi=soft) ;;
    esac
    for program in crunch dual; do
        arm-none-eabi-gcc $scalar $flags start.c $program.c -lgcc -o $program-$target.elf
    done
    if [[ $target != m0 && $target != m1 ]]; then
        arm-none-eabi-gcc $scalar $flags start.c exclusive.c -lgcc -o exclusive-$target.elf
    fi
    if [[ $target != m0 && $target != m1 && $target != m23 ]]; then
        arm-none-eabi-gcc $scalar $flags start.c isa.c -lgcc -o isa-$target.elf
    fi
done

arm-none-eabi-gcc -march=armv8-m.main+nodsp -mthumb -mfloat-abi=soft $flags start.c select.c -lgcc -o select-v81.elf

# irq.c for moved.hocon, whose console is at 0x50000000 and raises IRQ 7:
# `cortical run --board moved.hocon irq-moved.elf`.
arm-none-eabi-gcc $m0 $flags -DCONSOLE_BASE=0x50000000 -DCONSOLE_IRQ=7 start.c irq.c -lgcc -o irq-moved.elf

# What crunch.c prints built for the host, which both builds must print too.
cc -DHOST -O2 crunch.c -o crunch-host
./crunch-host > crunch.out
rm crunch-host

# The integer DSP guest: it runs on virt4 and virt7 as they are, and on the boards whose cores
# offer the extension as a choice with `dsp = true`, as dsp.hocon does.
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=soft $flags start.c dsp.c -lgcc -o dsp.elf
