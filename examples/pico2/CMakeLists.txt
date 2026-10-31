cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2 CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(benchtop_pico2 C CXX ASM)

pico_sdk_init()

# Every program is entered at `_entry_point` without going back through the boot ROM: crt0 sets up
# the RCP and MSPLIM itself and vectors through the image's own table. benchtop does not run the
# Raspberry Pi ROM's boot path.
function(benchtop_program target source type)
    add_executable(${target} ${source})
    target_link_libraries(${target} pico_stdlib)
    pico_set_binary_type(${target} ${type})
    target_compile_definitions(${target} PRIVATE PICO_CRT0_DEBUG_ENTRY_RESETS_VIA_BOOTROM=0)
    pico_enable_stdio_uart(${target} 1)
    pico_enable_stdio_usb(${target} 0)
endfunction()

# RAM images: the ELF is loaded into SRAM and entered at `_entry_point`, as a debugger does.
foreach(prog hello_serial blink rcp gpioc stack_limit tt interp clock_report dcp aon_timer rom otp hello_gpio_irq gpio_irq hello_uart uart_advanced uart_loopback hello_dma control_blocks dma_uart)
    benchtop_program(${prog} ${prog}.c no_flash)
endforeach()
target_link_libraries(hello_dma hardware_dma)
target_link_libraries(control_blocks hardware_dma)
target_link_libraries(dma_uart hardware_dma)
# hello_dma from flash too: the DMA reads its source through the XIP cache.
benchtop_program(hello_dma_flash hello_dma.c default)
target_link_libraries(hello_dma_flash hardware_dma)
target_link_libraries(gpio_irq pico_multicore)
# rom.c calls the boot ROM's functions, the flash's among them, from SRAM.
target_link_libraries(rom hardware_flash hardware_watchdog)
target_compile_definitions(rom PRIVATE PICO_ALWAYS_INCLUDE_FLASH_ID_FUNCTIONS=1)
target_link_libraries(aon_timer pico_aon_timer hardware_powman)

# rcp.c uses the redundancy coprocessor's header directly.
target_link_libraries(rcp hardware_rcp)
target_link_libraries(stack_limit hardware_exception)
# dcp.c calls the SDK's canned DCP sequences (dcp_ops.S) besides pico_double, and runs core 1.
target_sources(dcp PRIVATE dcp_ops.S)
target_link_libraries(dcp hardware_dcp pico_multicore)
# newlib's printf, whose dtoa converts exactly (the SDK's own printf stops at nine digits), so its %f,
# %e and %g are the host's, digit for digit, wherever the DCP's arithmetic is.
pico_set_printf_implementation(dcp compiler)
target_link_libraries(interp hardware_interp pico_multicore)
# ACLE's TTA and TTAT intrinsics are only declared for code built for the Security Extension.
target_compile_options(tt PRIVATE -mcmse)
# Non-secure code of its own: the Secure side built for the Security Extension, the Non-secure code,
# stack and veneer in nonsecure_ns.S.
benchtop_program(nonsecure nonsecure.c no_flash)
target_sources(nonsecure PRIVATE nonsecure_ns.S)
target_compile_options(nonsecure PRIVATE -mcmse)
target_link_libraries(nonsecure hardware_exception)

# Flash images, the SDK's default: the ELF's segments are written into the flash behind XIP_BASE.
foreach(prog hello_serial blink)
    benchtop_program(${prog}_flash ${prog}.c default)
endforeach()
pico_add_uf2_output(hello_serial_flash)

benchtop_program(qmi_flash qmi_flash.c default)

# A flash image, so that the boot ROM's normal boot after the watchdog's reset finds it again.
benchtop_program(watchdog watchdog.c default)
target_link_libraries(watchdog hardware_watchdog)
target_link_libraries(qmi_flash hardware_xip_cache)

# A flash image too: the boot ROM boots it again each time POWMAN powers the switched core up.
benchtop_program(powman powman.c default)
target_link_libraries(powman hardware_powman)

# USB device mode: stdio over the SDK's TinyUSB CDC device as well as UART0.
benchtop_program(usb_device usb_device.c no_flash)
pico_enable_stdio_usb(usb_device 1)
benchtop_program(usb_reconnect usb_reconnect.c no_flash)
pico_enable_stdio_usb(usb_reconnect 1)

# pico-examples' hello_world/usb as it builds there: a flash image, stdio over USB and not UART.
benchtop_program(hello_usb hello_usb.c default)
pico_enable_stdio_usb(hello_usb 1)
pico_enable_stdio_uart(hello_usb 0)

# pico-examples' usb/device/dev_lowlevel: the USB registers driven directly, stdio on UART0.
benchtop_program(dev_lowlevel dev_lowlevel.c default)
target_link_libraries(dev_lowlevel hardware_resets hardware_irq)

# The whole program in the XIP cache's 16 KB, as the SDK's own `pico_xip_sram_test` is built.
benchtop_program(xip_sram xip_sram.c xip_ram)
pico_minimize_runtime(xip_sram INCLUDE PRINTF PRINTF_MINIMAL DEFAULT_ALARM_POOL PANIC FLOAT)
target_compile_definitions(xip_sram PRIVATE PICO_HEAP_SIZE=0x200)

# A busy loop from XIP_SRAM, and the same loop from SRAM, for measuring the two paths side by side.
benchtop_program(xip_loop xip_loop.c xip_ram)
pico_minimize_runtime(xip_loop INCLUDE PRINTF PRINTF_MINIMAL DEFAULT_ALARM_POOL PANIC FLOAT)
target_compile_definitions(xip_loop PRIVATE PICO_HEAP_SIZE=0x200)
benchtop_program(sram_loop xip_loop.c no_flash)
pico_minimize_runtime(sram_loop INCLUDE PRINTF PRINTF_MINIMAL DEFAULT_ALARM_POOL PANIC FLOAT)

# The PIO programs: pioasm (built from the SDK's tools) assembles pio/*.pio into pio/*.pio.h, which are
# committed; pio_programs prints them with the configuration each pico-examples init builds.
benchtop_program(pio_programs pio_programs.c no_flash)
target_link_libraries(pio_programs hardware_pio)
pico_generate_pio_header(pio_programs ${CMAKE_CURRENT_LIST_DIR}/pio/hello.pio ${CMAKE_CURRENT_LIST_DIR}/pio/ws2812.pio ${CMAKE_CURRENT_LIST_DIR}/pio/uart_tx.pio ${CMAKE_CURRENT_LIST_DIR}/pio/uart_rx.pio ${CMAKE_CURRENT_LIST_DIR}/pio/rp2350_only.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/pio)

# pico-examples' PIO programs, run whole: their state machines through the mux. PIO_BLINK_FLASH_TIME_SECONDS
# is the program's own setting; a second of guest time shows all four rates and the clean shutdown.
foreach(prog pio_ws2812 pio_uart_tx pio_uart_rx pio_blink pio_uart_dma)
    benchtop_program(${prog} ${prog}.c no_flash)
    target_link_libraries(${prog} hardware_pio hardware_clocks)
    target_include_directories(${prog} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/pio)
    add_dependencies(${prog} pio_programs)
endforeach()
target_link_libraries(pio_uart_rx pico_multicore)
target_link_libraries(pio_uart_dma hardware_dma)
target_compile_definitions(pio_blink PRIVATE PIO_BLINK_FLASH_TIME_SECONDS=1)
pico_generate_pio_header(pio_blink ${CMAKE_CURRENT_LIST_DIR}/pio/blink.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/pio)
