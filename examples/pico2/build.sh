#!/bin/sh
# Rebuilds the Arm Pico 2 examples (README.md lists them) with the Pico SDK and Arm's arm-none-eabi
# toolchain. PICO_SDK_PATH defaults to the org's SDK checkout (README.md names its commit).
set -eu
cd "$(dirname "$0")"
export PICO_SDK_PATH=${PICO_SDK_PATH:-$HOME/dev/sysl-lang/pico-scratch/pico-sdk}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cmake -S . -B "$work" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$work"
for prog in hello_serial blink rcp gpioc stack_limit tt interp clock_report dcp aon_timer rom otp hello_serial_flash blink_flash qmi_flash xip_sram xip_loop sram_loop watchdog powman usb_device usb_reconnect hello_usb dev_lowlevel hello_gpio_irq gpio_irq hello_uart uart_advanced uart_loopback nonsecure pio_programs hello_dma hello_dma_flash control_blocks dma_uart dvi_out_hstx_encoder hstx_spi_lcd; do
    cp "$work/$prog.elf" "$prog.elf"
done
cp "$work/hello_serial_flash.uf2" hello_serial_flash.uf2
