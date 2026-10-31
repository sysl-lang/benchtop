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
foreach(prog hello_adc onboard_temperature)
    benchtop_program(${prog} ${prog}.c no_flash)
    target_link_libraries(${prog} hardware_adc)
endforeach()
target_link_libraries(rtc_alarm hardware_rtc)
# rom_crosscheck: a flash image with a UF2, for a real Pico as well as benchtop, stdio on UART0 and USB.
# benchtop's own ROM routines are compiled into it by sysl from rom_crosscheck/ours, an archive it links.
find_program(SYSL_COMPILER sysl REQUIRED)
set(OURS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/rom_crosscheck/ours)
set(OURS_LIB ${CMAKE_CURRENT_BINARY_DIR}/libours.a)
file(GLOB_RECURSE OURS_SOURCES ${OURS_DIR}/*.sysl ${OURS_DIR}/sysl.hocon ${CMAKE_CURRENT_SOURCE_DIR}/../../sh/sysl/benchtop/chip/rom/rp2040/sh/sysl/*.sysl)
add_custom_command(OUTPUT ${OURS_LIB}
    COMMAND ${SYSL_COMPILER} build-c ${OURS_DIR} --target thumbv6m-freestanding -Oz -o ${OURS_LIB}
    DEPENDS ${OURS_SOURCES} ${SYSL_COMPILER})
add_custom_target(ours_lib DEPENDS ${OURS_LIB})
add_executable(rom_crosscheck rom_crosscheck/rom_crosscheck.c)
add_dependencies(rom_crosscheck ours_lib)
target_link_libraries(rom_crosscheck pico_stdlib ${OURS_LIB})
pico_enable_stdio_uart(rom_crosscheck 1)
pico_enable_stdio_usb(rom_crosscheck 1)
pico_add_uf2_output(rom_crosscheck)

# The SDK's own float and double tests (sdk_tests/, from its test/pico_float_test), built as its
# CMakeLists builds them for the default implementation, `pico`, which on the RP2040 is the boot ROM's:
# flash images, stdio on UART0.
foreach(prog pico_float_test pico_double_test)
    add_executable(${prog} sdk_tests/${prog}.c sdk_tests/llvm/call_apsr.S)
    target_include_directories(${prog} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sdk_tests/llvm)
    pico_enable_stdio_uart(${prog} 1)
    pico_enable_stdio_usb(${prog} 0)
endforeach()
target_link_libraries(pico_float_test pico_float pico_stdlib m)
pico_set_float_implementation(pico_float_test pico)
target_link_libraries(pico_double_test pico_double pico_stdlib m)
pico_set_double_implementation(pico_double_test pico)
target_compile_definitions(pico_double_test PRIVATE PICO_USE_CRT_PRINTF=1 PICO_FLOAT_PROPAGATE_NANS=1 PICO_DOUBLE_PROPAGATE_NANS=1)
pico_set_printf_implementation(pico_double_test compiler)
foreach(kind float double)
    set(prog custom_${kind}_funcs_test)
    add_executable(${prog} sdk_tests/${prog}.c)
    target_link_libraries(${prog} PRIVATE pico_stdlib)
    pico_set_printf_implementation(${prog} compiler)
    pico_enable_stdio_uart(${prog} 1)
    pico_enable_stdio_usb(${prog} 0)
endforeach()
pico_set_float_implementation(custom_float_funcs_test pico)
pico_set_double_implementation(custom_double_funcs_test pico)

# ssi_flash enters XIP through the SDK's own second stage, assembled into the RAM image and called.
target_sources(ssi_flash PRIVATE $ENV{PICO_SDK_PATH}/src/rp2040/boot_stage2/boot2_w25q080.S)
target_include_directories(ssi_flash PRIVATE $ENV{PICO_SDK_PATH}/src/rp2040/boot_stage2/asminclude)
target_link_libraries(ssi_flash hardware_vreg)
