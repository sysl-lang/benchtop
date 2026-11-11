cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(benchtop_pico2w C CXX ASM)

pico_sdk_init()

# As examples/pico2: entered at `_entry_point` from SRAM, as a debugger does, without the boot ROM.
function(benchtop_program target source type)
    add_executable(${target} ${source})
    target_link_libraries(${target} pico_stdlib)
    pico_set_binary_type(${target} ${type})
    target_compile_definitions(${target} PRIVATE PICO_CRT0_DEBUG_ENTRY_RESETS_VIA_BOOTROM=0)
    pico_enable_stdio_uart(${target} 1)
    pico_enable_stdio_usb(${target} 0)
endfunction()

# blink: the LED on the CYW43439's GPIO 0 through pico_cyw43_arch_none, its firmware in the image.
benchtop_program(blink blink.c no_flash)
target_link_libraries(blink pico_cyw43_arch_none)
