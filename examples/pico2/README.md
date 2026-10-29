# Pico 2 (Arm) examples

Pico SDK programs that the `pico2` board runs on its Cortex-M33s, and that
`chip/arm_tests.sysl` runs end to end:

| program | what it does | what the test asserts |
|---|---|---|
| `hello_serial.c` | `stdio_init_all()` on UART0, then `hello, serial N` every 100 ms | the bytes UART0 sends, a line every 100 ms of guest time |
| `blink.c` | GPIO 25 (the LED) toggled every 250 ms with `sleep_ms` | GPIO 25's level, an edge every 250 ms of guest time |
| `rcp.c` | every `hardware/rcp.h` function and macro, both forms, passing; then a corrupted canary | its lines on UART0, then core 0 stopped at `Fault` with `rcp_canary_check` named |
| `gpioc.c` | every `hardware/gpio_coproc.h` function, each compared with its SIO equivalent | `87 checks, 0 mismatches` and the final OUT and OE |

```sh
./benchtop run --board pico2 examples/pico2/hello_serial.elf
```

## Building

`build.sh` rebuilds the ELFs, which are committed because the tests embed them:

```sh
sh examples/pico2/build.sh
```

It needs CMake, Ninja, `picotool` and Arm's `arm-none-eabi` toolchain (the `gcc-arm-embedded` cask;
the ELFs here were built with Arm GNU Toolchain 15.3.Rel2). The SDK is
`~/dev/sysl-lang/pico-scratch/pico-sdk` unless `PICO_SDK_PATH` names another: **SDK 2.3.0, commit
`98a542c1a62fb549ffb5d66a3e5892b06276b670`**.

The settings, all in `CMakeLists.txt`:

- `PICO_BOARD=pico2`, `PICO_PLATFORM=rp2350-arm-s`;
- `pico_set_binary_type(... no_flash)`: a RAM image, which benchtop loads into SRAM and enters at
  the ELF entry as a debugger does. Flash (XIP) is not on the board's bus yet;
- stdio on UART0, not USB;
- `PICO_CRT0_DEBUG_ENTRY_RESETS_VIA_BOOTROM=0`. By default an RP2350 RAM image entered by a debugger
  goes back through the boot ROM's RAM-image boot (watchdog scratch registers, then the ROM's vector
  table) to be launched again; benchtop does not run the Raspberry Pi ROM, so the image instead
  initialises the redundancy coprocessor and MSPLIM itself and vectors through its own table, the
  SDK's other supported entry.
