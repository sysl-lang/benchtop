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
| `rom_funcs.c` | benchtop's own: `rom_func_lookup` of the bit functions, memset and memcpy, `rom_data_lookup` of the soft float and double tables, each called; then the SDK's own float and double divide, which go through the ROM | every line, against the host's arithmetic |
| `hello_world_serial.c` | pico-examples' `hello_world/serial` (commit 0d62f75), unchanged, **a flash image**: the boot ROM checks boot2's CRC, runs it, and boot2 enters the image | `Hello, world!` every second of guest time; XIP left reading the flash |
| `flash_program.c` | pico-examples' `flash/program` (commit 0d62f75), unchanged, a flash image: a sector erased and a page programmed 256 KB in through the ROM's flash functions | every line, and the page in the flash array |
| `hello_watchdog.c` | pico-examples' `watchdog/hello_watchdog` (commit 0d62f75), unchanged, a flash image: the watchdog's reset boots it again through the ROM | `Clean boot`, the five updates, then `Rebooted by Watchdog!` |
| `bus_scan.c` | pico-examples' `i2c/bus_scan` (commit 0d62f75), unchanged: I2C0 on GPIO 4 and 5 at 100 kHz reads one byte from every address that is not reserved | the table, `@` at 0x68 for the DS3231 the test puts on GPIO 4 and 5 (`chip/i2c_tests.sysl`) |
| `sdk_tests/` | the Pico SDK's own `test/pico_float_test` programs (SDK 2.3.0), unchanged but for a first line naming their origin: `pico_float_test`, `pico_double_test`, `custom_float_funcs_test`, `custom_double_funcs_test`, flash images on the SDK's default implementation, the ROM's | `PASSED` |
| `rom_crosscheck/` | benchtop's own: the chip's ROM against benchtop's ROM routines compiled into the firmware, bit for bit; a flash image and a UF2 for a real Pico (below) | every function's line with no mismatch, and `CROSSCHECK 71248 0 8c217b56` |

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
- `pico_set_binary_type(... no_flash)` for benchtop's own programs and the first three: a RAM image,
  which benchtop loads into SRAM and enters at the ELF entry as a debugger does. `blink` is also a RAM
  UF2 (`pico_add_uf2_output`), which benchtop enters at its first byte as the ROM does. The three
  pico-examples programs that are flash images (`benchtop_flash_program`) are built as their own
  CMakeLists build them, boot2 and all, and boot through the ROM;
- `ssi_flash` links the SDK's own `src/rp2040/boot_stage2/boot2_w25q080.S` into its RAM image and
  calls it, as the SDK's `flash_enable_xip_via_boot2` calls its copy;
- stdio on UART0, not USB (`rom_crosscheck` has both);
- otherwise the SDK's defaults: float, double, the bit operations and memcpy/memset from the boot ROM,
  which on benchtop is benchtop's own (`sh/sysl/benchtop/chip/rom/README.md`), and the SIO's hardware
  divider;
- `rom_crosscheck` links an archive `sysl build-c` makes from `rom_crosscheck/ours` (sysl 0.8.7 on
  the PATH), benchtop's ROM routines compiled for the Cortex-M0+.

## The ROM cross-check on a real Pico

`rom_crosscheck` calls every soft float and double entry and the bit and memory functions of the
chip's own boot ROM beside benchtop's ROM routines, compiled into the firmware, on a fixed, seeded set
of inputs, and compares them bit for bit. On benchtop both sides are benchtop's, every case agrees,
and the last line is `CROSSCHECK 71248 0 8c217b56` (`chip/pico_tests.sysl`). On a Pico the chip's ROM
is Raspberry Pi's: the mismatches say where the two ROMs differ, and the CRC, of benchtop's answers
alone, must again be `8c217b56`.

To run it, with the Pico not yet connected:

1. Hold BOOTSEL and plug the Pico in; it mounts as `RPI-RP2`.
2. Copy `examples/pico/rom_crosscheck.uf2` onto it. The Pico reboots into the firmware.
3. Open its USB serial port (`/dev/cu.usbmodem*` on a Mac, e.g. `screen /dev/cu.usbmodem1101 115200`).
   The firmware waits for the port to open, runs once (a minute or so), then repeats its `CROSSCHECK`
   line every five seconds. The same output is on UART0 (GPIO 0) at 115200 baud.
4. Paste the whole output back.

With the Pico plugged into this Mac, `picotool` does steps 1 and 2:

```sh
picotool load -x examples/pico/rom_crosscheck.uf2
```

(BOOTSEL held at plug-in the first time, since the Pico's current firmware may not answer picotool.)
