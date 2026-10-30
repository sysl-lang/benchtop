cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico CACHE STRING "Board type")
set(PICO_PLATFORM rp2040 CACHE STRING "Platform")

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(benchtop_pico C CXX ASM)

pico_sdk_init()

# Every program is a RAM image entered at `_entry_point`, as a debugger enters it. benchtop's RP2040
# boot ROM is a stand-in that carries no function (README.md), so the SDK's helpers that it would look
# up through the ROM's table are the compiler's own: float, double, the bit operations and memcpy and
# memset. The divider stays the SIO's hardware divider, the SDK's default.
function(benchtop_program target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} pico_stdlib)
    pico_set_binary_type(${target} no_flash)
    pico_enable_stdio_uart(${target} 1)
    pico_enable_stdio_usb(${target} 0)
    pico_set_float_implementation(${target} compiler)
    pico_set_double_implementation(${target} compiler)
    pico_set_bit_ops_implementation(${target} compiler)
    pico_set_mem_ops_implementation(${target} compiler)
endfunction()

foreach(prog hello_serial blink hello_multicore hello_rtc rtc_alarm ssi_flash)
    benchtop_program(${prog} ${prog}.c)
endforeach()
target_link_libraries(hello_multicore pico_multicore)
# blink also as a UF2, the RAM image the boot ROM's USB drive takes and enters at its first byte.
pico_add_uf2_output(blink)
target_link_libraries(hello_rtc hardware_rtc)
target_link_libraries(rtc_alarm hardware_rtc)
# ssi_flash enters XIP through the SDK's own second stage, assembled into the RAM image and called.
target_sources(ssi_flash PRIVATE $ENV{PICO_SDK_PATH}/src/rp2040/boot_stage2/boot2_w25q080.S)
target_include_directories(ssi_flash PRIVATE $ENV{PICO_SDK_PATH}/src/rp2040/boot_stage2/asminclude)
target_link_libraries(ssi_flash hardware_vreg)
