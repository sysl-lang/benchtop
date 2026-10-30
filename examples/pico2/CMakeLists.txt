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
foreach(prog hello_serial blink rcp gpioc stack_limit tt interp clock_report dcp aon_timer rom)
    benchtop_program(${prog} ${prog}.c no_flash)
endforeach()
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
