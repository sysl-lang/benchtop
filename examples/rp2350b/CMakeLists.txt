cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pimoroni_pico_plus2_rp2350 CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(benchtop_rp2350b C CXX ASM)

pico_sdk_init()

# Entered at `_entry_point` without going back through the boot ROM, as the Pico 2's examples are.
add_executable(high_bank high_bank.c)
target_link_libraries(high_bank pico_stdlib hardware_adc)
pico_set_binary_type(high_bank no_flash)
target_compile_definitions(high_bank PRIVATE PICO_CRT0_DEBUG_ENTRY_RESETS_VIA_BOOTROM=0)
pico_enable_stdio_uart(high_bank 1)
pico_enable_stdio_usb(high_bank 0)
