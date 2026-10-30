# Pico (RP2040) examples

Pico SDK programs that the `pico` board runs on its Cortex-M0+s, and that `chip/pico_tests.sysl` runs
end to end:

| program | what it does | what the test asserts |
|---|---|---|
| `hello_serial.c` | `stdio_init_all()` on UART0, then `hello, serial N` every 100 ms | the bytes UART0 sends, a line every 100 ms of guest time at 125 MHz; the watchdog's TICK and USB's SIE_CTRL as runtime_init leaves them |
| `blink.c` | GPIO 25 (the LED) toggled every 250 ms with `sleep_ms` | GPIO 25's level, an edge every 250 ms of guest time |
| `hello_multicore.c` | pico-examples' `multicore/hello_multicore` (commit 0d62f75), unchanged: core 1 launched through the boot ROM's FIFO wait, a word each way | `Hello, multicore!`, then each core's line (core 0 stopped at its exit BKPT while core 1 runs on) |
| `hello_rtc.c` | pico-examples' `rtc/hello_rtc` (commit 0d62f75), unchanged: the RTC set to Friday 5 June 2020 15:45:00 and read every 100 ms | `Hello RTC!`, then the date and time, a second on every second of guest time |
| `rtc_alarm.c` | pico-examples' `rtc/rtc_alarm` (commit 0d62f75), unchanged: an alarm five seconds on, through RTC_IRQ | `RTC Alarm!`, `Alarm Fired At Wednesday 13 January 11:20:05 2020` five seconds of guest time on |
| `ssi_flash.c` | benchtop's own: the flash through the SSI with no ROM function, the SDK's `flash_do_cmd` sequence (chip select forced through IO_QSPI) for the IDs, an erase and two page programs; the SDK's boot2 into quad XIP; the four XIP aliases, code run from flash and again after reprogramming, the stream, the cache as SRAM, VREG | every line it prints (`chip/pico_xip_tests.sysl`) |
| `blink.uf2` | `blink` as a RAM UF2 | GPIO 25 toggling, as the ELF does |

pico-examples' `flash/program` is not here: it erases and programs through the boot ROM's flash
functions (`flash_range_erase` and the rest look them up with `rom_func_lookup`), which the stand-in
ROM refuses until item 23 maps Raspberry Pi's. `ssi_flash` drives the same SSI sequences itself.

```sh
./benchtop run --board pico examples/pico/hello_serial.elf
```

## Building

`build.sh` rebuilds every ELF, which are committed because the tests embed them:

```sh
sh examples/pico/build.sh
```

It needs CMake, Ninja and Arm's `arm-none-eabi` toolchain (Arm GNU Toolchain 15.3.Rel2). The SDK is
`~/dev/sysl-lang/pico-scratch/pico-sdk` unless `PICO_SDK_PATH` names another: **SDK 2.3.0, commit
`98a542c1a62fb549ffb5d66a3e5892b06276b670`**.

The settings, all in `CMakeLists.txt`:

- `PICO_BOARD=pico`, `PICO_PLATFORM=rp2040`;
- `pico_set_binary_type(... no_flash)`: a RAM image, which benchtop loads into SRAM and enters at the
  ELF entry as a debugger does (a flash image starts through the boot ROM's boot path, boot2 from
  flash, which benchtop's stand-in ROM does not carry: docs/rp-coverage.md item 23). `blink` is also
  a RAM UF2 (`pico_add_uf2_output`), which benchtop enters at its first byte as the ROM does;
- `ssi_flash` links the SDK's own `src/rp2040/boot_stage2/boot2_w25q080.S` into its RAM image and
  calls it, as the SDK's `flash_enable_xip_via_boot2` calls its copy;
- stdio on UART0, not USB;
- **the compiler's float, double, bit operations and memcpy/memset** (`pico_set_float_implementation`,
  `..._double_...`, `..._bit_ops_...`, `..._mem_ops_...` with `compiler`). The SDK's defaults look these
  up in the RP2040's boot ROM, and benchtop's ROM is a stand-in that carries only core 1's launch wait
  and refuses a lookup by name; Raspberry Pi's ROM is item 23. The divider is the SDK's default, the
  SIO's hardware divider.
