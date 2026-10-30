# Pico 2 (Arm) examples

Pico SDK programs that the `pico2` board runs on its Cortex-M33s, and that
`chip/arm_tests.sysl` runs end to end:

| program | what it does | what the test asserts |
|---|---|---|
| `hello_serial.c` | `stdio_init_all()` on UART0, then `hello, serial N` every 100 ms | the bytes UART0 sends, a line every 100 ms of guest time |
| `blink.c` | GPIO 25 (the LED) toggled every 250 ms with `sleep_ms` | GPIO 25's level, an edge every 250 ms of guest time |
| `aon_timer.c` | `pico_aon_timer` and `hardware_powman`: `aon_timer_start` from the crystal at a wall-clock time, `aon_timer_get_time` after `sleep_ms`, an alarm waited for with WFI, `powman_clear_alarm` enabled and disabled, an alarm already past, `aon_timer_stop`, and the carry past 2^32 ms | every line printed over UART0 (`chip/aon_tests.sysl`) |
| `clock_report.c` | `clock_get_hz` and `frequency_count_khz` for each clock, SYSINFO's chip id, PSM DONE, 32 ROSC random bits, the regulator | every line printed over UART0 (`chip/startup_tests.sysl`) |
| `hello_serial_flash`, `blink_flash` (and `hello_serial_flash.uf2`) | the same two built as flash images, the SDK's default | the same output, run in place from flash (`chip/xip_tests.sysl`) |
| `qmi_flash.c` | reads the flash's JEDEC and unique IDs, erases a sector 1 MB in and programs a page through QMI direct mode from SRAM, then reads it back through the cached, uncached and untranslated windows, the streaming FIFO and direct mode, and pins two XIP cache lines as SRAM | each `ok` line, `PASSED`, and the flash's bytes |
| `xip_sram.c` | the whole program in the XIP cache's 16 KB (`xip_ram`), as the SDK's `pico_xip_sram_test` | `main` and the stack at 0x13ffc000 on, five lines 100 ms apart, `PASSED` |
| `xip_loop.c` (and `sram_loop`, the same source built for SRAM) | 20000 rounds of a sort and a checksum with no waiting, from XIP_SRAM and from SRAM, for measuring the two paths side by side | `checksum d0a54a76`, `PASSED` |
| `stack_limit.c` | MSPLIM 1 KB below SP, then a 2 KB frame: the UsageFault STKOF in its own handler, which lifts the limit so the `SUB` runs again | its report: one fault, CFSR's STKOF, the faulting PC in `deep` |
| `tt.c` | ACLE's `cmse_TT`/`TTT`/`TTA`/`TTAT` of SRAM and the PPB, before and after two SAU regions (built with `-mcmse`) | each answer's word |
| `rcp.c` | every `hardware/rcp.h` function and macro, both forms, passing; then a corrupted canary | its lines on UART0, then core 0 stopped at `Fault` with `rcp_canary_check` named |
| `gpioc.c` | every `hardware/gpio_coproc.h` function, each compared with its SIO equivalent | `87 checks, 0 mismatches` and the final OUT and OE |
| `interp.c` | `hardware_interp` in each mode (counter, blend unsigned and signed, clamp, rotate, FORCE_MSB, ADD_RAW, cross, save/restore); core 1 counting on its own INTERP0 at the same time; the multicore doorbells, each core's SIO_IRQ_BELL handler acknowledging | each line's values, worked from the datasheet (`chip/interp_tests.sysl`) |

```sh
./benchtop run --board pico2 examples/pico2/hello_serial.elf
```

## Building

`build.sh` rebuilds every ELF and the UF2, which are committed because the tests embed them:

```sh
sh examples/pico2/build.sh
```

It needs CMake, Ninja, `picotool` and Arm's `arm-none-eabi` toolchain (the `gcc-arm-embedded` cask;
the ELFs here were built with Arm GNU Toolchain 15.3.Rel2). The SDK is
`~/dev/sysl-lang/pico-scratch/pico-sdk` unless `PICO_SDK_PATH` names another: **SDK 2.3.0, commit
`98a542c1a62fb549ffb5d66a3e5892b06276b670`**.

The settings, all in `CMakeLists.txt`:

- `PICO_BOARD=pico2`, `PICO_PLATFORM=rp2350-arm-s`;
- `pico_set_binary_type(... no_flash)` for `hello_serial` and `blink`: a RAM image, which benchtop
  loads into SRAM and enters at the ELF entry as a debugger does; `default` for the flash builds,
  whose segments benchtop writes into the flash; `xip_ram` for `xip_sram` and `xip_loop`;
- stdio on UART0, not USB;
- `PICO_CRT0_DEBUG_ENTRY_RESETS_VIA_BOOTROM=0`. By default an RP2350 RAM image entered by a debugger
  goes back through the boot ROM's RAM-image boot (watchdog scratch registers, then the ROM's vector
  table) to be launched again; benchtop does not run the Raspberry Pi ROM, so the image instead
  initialises the redundancy coprocessor and MSPLIM itself and vectors through its own table, the
  SDK's other supported entry.
