cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2 CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(benchtop_pico2 C CXX ASM)

pico_sdk_init()

foreach(prog hello_serial blink rcp gpioc stack_limit tt dcp)
    add_executable(${prog} ${prog}.c)
    target_link_libraries(${prog} pico_stdlib)
    # A RAM image: the ELF is loaded into SRAM and entered at `_entry_point`, as a debugger does.
    pico_set_binary_type(${prog} no_flash)
    # Entered at `_entry_point` without going back through the boot ROM: crt0 sets up the RCP and
    # MSPLIM itself and vectors through the image's own table. benchtop does not run the ROM yet.
    target_compile_definitions(${prog} PRIVATE PICO_CRT0_DEBUG_ENTRY_RESETS_VIA_BOOTROM=0)
    pico_enable_stdio_uart(${prog} 1)
    pico_enable_stdio_usb(${prog} 0)
endforeach()

# rcp.c uses the redundancy coprocessor's header directly.
target_link_libraries(rcp hardware_rcp)
target_link_libraries(stack_limit hardware_exception)
# dcp.c calls the SDK's canned DCP sequences (dcp_ops.S) besides pico_double, and runs core 1.
target_sources(dcp PRIVATE dcp_ops.S)
target_link_libraries(dcp hardware_dcp pico_multicore)
# newlib's printf, whose dtoa converts exactly (the SDK's own printf stops at nine digits), so its %f,
# %e and %g are the host's, digit for digit, wherever the DCP's arithmetic is.
pico_set_printf_implementation(dcp compiler)
# ACLE's TTA and TTAT intrinsics are only declared for code built for the Security Extension.
target_compile_options(tt PRIVATE -mcmse)
