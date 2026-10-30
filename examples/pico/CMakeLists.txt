cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico CACHE STRING "Board type")
set(PICO_PLATFORM rp2040 CACHE STRING "Platform")

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(benchtop_pico C CXX ASM)

pico_sdk_init()

# Every program is a RAM image entered at `_entry_point`, as a debugger enters it, with the SDK's
# defaults otherwise: float, double, the bit operations and memcpy/memset from the boot ROM, and the
# SIO's hardware divider.
function(benchtop_program target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} pico_stdlib)
    pico_set_binary_type(${target} no_flash)
    pico_enable_stdio_uart(${target} 1)
    pico_enable_stdio_usb(${target} 0)
endfunction()

foreach(prog hello_serial blink hello_multicore hello_rtc rtc_alarm ssi_flash rom_funcs)
    benchtop_program(${prog} ${prog}.c)
endforeach()

# pico-examples' programs as their own CMakeLists build them: flash images, with boot2 in the first
# 256 bytes, which the boot ROM's boot path checks and runs.
function(benchtop_flash_program target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} pico_stdlib)
    pico_enable_stdio_uart(${target} 1)
    pico_enable_stdio_usb(${target} 0)
endfunction()

benchtop_flash_program(hello_world_serial hello_world_serial.c)
benchtop_flash_program(flash_program flash_program.c)
benchtop_flash_program(hello_watchdog hello_watchdog.c)
target_link_libraries(flash_program hardware_flash)
target_link_libraries(hello_multicore pico_multicore)
# blink also as a UF2, the RAM image the boot ROM's USB drive takes and enters at its first byte.
pico_add_uf2_output(blink)
target_link_libraries(hello_rtc hardware_rtc)
target_link_libraries(rtc_alarm hardware_rtc)
# ssi_flash enters XIP through the SDK's own second stage, assembled into the RAM image and called.
target_sources(ssi_flash PRIVATE $ENV{PICO_SDK_PATH}/src/rp2040/boot_stage2/boot2_w25q080.S)
target_include_directories(ssi_flash PRIVATE $ENV{PICO_SDK_PATH}/src/rp2040/boot_stage2/asminclude)
target_link_libraries(ssi_flash hardware_vreg)
