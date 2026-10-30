# Pico (RP2040) examples

Pico SDK programs that the `pico` board runs on its Cortex-M0+s, and that `chip/pico_tests.sysl` runs
end to end:

| program | what it does | what the test asserts |
|---|---|---|
| `hello_serial.c` | `stdio_init_all()` on UART0, then `hello, serial N` every 100 ms | the bytes UART0 sends, a line every 100 ms of guest time at 125 MHz; the watchdog's TICK and USB's SIE_CTRL as runtime_init leaves them |
| `blink.c` | GPIO 25 (the LED) toggled every 250 ms with `sleep_ms` | GPIO 25's level, an edge every 250 ms of guest time |
| `hello_multicore.c` | pico-examples' `multicore/hello_multicore` (commit 0d62f75), unchanged: core 1 launched through the boot ROM's FIFO wait, a word each way | `Hello, multicore!`, then each core's line (core 0 stopped at its exit BKPT while core 1 runs on) |

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
  ELF entry as a debugger does (the RP2040's flash is read through the SSI, which is not on the chip
  yet: docs/rp-coverage.md item 24);
- stdio on UART0, not USB;
- **the compiler's float, double, bit operations and memcpy/memset** (`pico_set_float_implementation`,
  `..._double_...`, `..._bit_ops_...`, `..._mem_ops_...` with `compiler`). The SDK's defaults look these
  up in the RP2040's boot ROM, and benchtop's ROM is a stand-in that carries only core 1's launch wait
  and refuses a lookup by name; Raspberry Pi's ROM is item 23. The divider is the SDK's default, the
  SIO's hardware divider.
