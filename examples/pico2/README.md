# Pico 2 (Arm) examples

Pico SDK programs that the `pico2` board runs on its Cortex-M33s, and that
`chip/arm_tests.sysl` runs end to end:

| program | what it does | what the test asserts |
|---|---|---|
| `hello_serial.c` | `stdio_init_all()` on UART0, then `hello, serial N` every 100 ms | the bytes UART0 sends, a line every 100 ms of guest time |
| `blink.c` | GPIO 25 (the LED) toggled every 250 ms with `sleep_ms` | GPIO 25's level, an edge every 250 ms of guest time |
| `aon_timer.c` | `pico_aon_timer` and `hardware_powman`: `aon_timer_start` from the crystal at a wall-clock time, `aon_timer_get_time` after `sleep_ms`, an alarm waited for with WFI, `powman_clear_alarm` enabled and disabled, an alarm already past, `aon_timer_stop`, and the carry past 2^32 ms | every line printed over UART0 (`chip/aon_tests.sysl`) |
| `hello_gpio_irq.c` | pico-examples' `gpio/hello_gpio_irq`, unchanged: every rising and falling edge on GPIO 2 printed by the SDK's GPIO callback, then a spin for ever | `Hello GPIO IRQ`, then one line per edge the test drives onto GPIO 2 from outside, in order (`chip/gpio_tests.sysl`) |
| `hello_uart.c` | pico-examples' `uart/hello_uart` (commit 0d62f75), unchanged: `A`, `B` and ` Hello, UART!` written straight to UART0 at 115200 baud, then a return from `main` with characters still in the FIFO | `AB Hello, UART!`, one character a frame (13020 cycles at 150 MHz), the rest sent after the breakpoint (`chip/uart_tests.sysl`) |
| `uart_advanced.c` | pico-examples' `uart/uart_advanced` (commit 0d62f75), unchanged: UART0 at 2400 then 115200 baud with its FIFOs off, and an RX interrupt that sends each character back one higher | its greeting, then `IBM` for the `HAL` the test sends into GPIO 1 (`chip/uart_tests.sysl`) |
| `uart_loopback.c` | UART0 and UART1 over two wires the test makes (GPIO 12 to 5, GPIO 4 to 13): UART1 hears the console's `ping` and its receive timeout fires, timed by the 1 µs timer; `pong` back; a break; an overrun of UART0's FIFO | every line, the timeout's 804 µs among them (`chip/uart_tests.sysl`) |
| `bus_scan.c` | pico-examples' `i2c/bus_scan` (commit 0d62f75), unchanged: I2C0 on GPIO 4 and 5 at 100 kHz reads one byte from every address that is not reserved | the table, `@` at 0x68 for the DS3231 the test puts on GPIO 4 and 5 (`chip/i2c_tests.sysl`) |
| `slave_mem_i2c.c` | pico-examples' `i2c/slave_mem_i2c` (commit 0d62f75), unchanged: I2C0 a slave memory on GPIO 4 and 5 through pico_i2c_slave's interrupt handler, I2C1 the master on GPIO 6 and 7, every 2 s a message written and read back in two parts | every line of the first three rounds, over the two wires the test makes (4 to 6, 5 to 7) (`chip/i2c_tests.sysl`) |
| `hello_dma.c` | pico-examples' `dma/hello_dma` (commit 0d62f75), unchanged: one unpaced channel copies a string, the core waits on BUSY and prints the copy; built from SRAM (`hello_dma.elf`) and from flash (`hello_dma_flash.elf`, the DMA reading through XIP) | `Hello, world! (from DMA)` from both (`chip/dma_tests.sysl`) |
| `control_blocks.c` | pico-examples' `dma/control_blocks` (commit 0d62f75), unchanged: a control channel programs a data channel through its alias 3 and a write ring; the data channel gathers six words to UART0 paced by its TX DREQ; a null block ends the chain | its two lines with `Transferring one word at a time.` between them (`chip/dma_tests.sysl`) |
| `spi_dma.c` | pico-examples' spi/spi_dma, unchanged: SPI0 in loopback, 1024 bytes out and back by DMA paced by its TX and RX requests | every line of its output, "All good" included (`chip/spi_tests.sysl`) |
| `dma_uart.c` | UART0 by DMA with its end taken as DMA_IRQ_1 in `WFI`; a pacing timer's rate; a write ring; an abort mid-sequence; a chain | every line: the interrupt 1135 µs after the start (13 frames after the FIFO's 33), 200 words at 1/150 in 200 µs, the ring's last two words, 80 left after the abort (`chip/dma_tests.sysl`) |
| `hello_adc.c` | pico-examples' `adc/hello_adc` (commit 0d62f75), unchanged: GPIO 26 converted every 500 ms | the raw code and voltage for the level the test puts on GPIO 26, then for a new one half a second on (`chip/adc_tests.sysl`) |
| `onboard_temperature.c` | pico-examples' `adc/onboard_temperature` (commit 0d62f75), unchanged: the temperature sensor on input 4 | `Onboard temperature = 40.25 C` for the board's 40 °C (`chip/adc_tests.sysl`) |
| `adc_console.c` | pico-examples' `adc/adc_console` (commit 0d62f75), unchanged: a UART0 console selecting inputs, sampling once and capturing 1000 samples through the FIFO | every line for `c2s`, `c9c4sx` and `c2S` typed into GPIO 1 (`chip/adc_tests.sysl`) |
| `dma_capture.c`, `dma_capture_adc.c` | pico-examples' `adc/dma_capture` (commit 0d62f75), unchanged, included by `dma_capture_adc.c` with core 1's PIO DAC left unlaunched: 1000 samples at 500 kS/s moved by the DMA on the ADC's DREQ | every line: the 1000 8-bit samples of the level the test puts on GPIO 26 (`chip/adc_tests.sysl`) |
| `gpio_irq.c` | `hardware_gpio`'s interrupts with nothing outside: GPIO 5 an SIO output read back through its pad; edges one at a time and two in one interrupt, a level and an input that is off reading low, a forced interrupt, IRQOVER and INOVER, core 1's own enables and callback, the dormant wake bringing the ROSC out of DORMANT | each line: the core, GPIO and event mask each callback saw, the raw INTR nibbles, the ROSC's DORMANT (`chip/gpio_tests.sysl`) |
| `clock_report.c` | `clock_get_hz` and `frequency_count_khz` for each clock, SYSINFO's chip id, PSM DONE, 32 ROSC random bits, the regulator | every line printed over UART0 (`chip/startup_tests.sysl`) |
| `hello_serial_flash`, `blink_flash` (and `hello_serial_flash.uf2`) | the same two built as flash images, the SDK's default | the same output, run in place from flash (`chip/xip_tests.sysl`) |
| `qmi_flash.c` | reads the flash's JEDEC and unique IDs, erases a sector 1 MB in and programs a page through QMI direct mode from SRAM, then reads it back through the cached, uncached and untranslated windows, the streaming FIFO and direct mode, and pins two XIP cache lines as SRAM | each `ok` line, `PASSED`, and the flash's bytes |
| `watchdog.c` | `hardware_watchdog` across four boots of one flash image: fed three times then starved, `watchdog_reboot(0, 0, 0)` (TRIGGER), then a PC/SP `watchdog_reboot` to a function that records the boot type | each boot's REASON, `watchdog_caused_reboot`, `watchdog_enable_caused_reboot`; the reset 100 ms after the last feed; SCRATCH1/2 from the PC/SP boot (`chip/watchdog_tests.sysl`) |
| `powman.c` | `hardware_powman` across three power-ups of one flash image: an invalid request and one ignored for an asserted GPIO request, P1.7 until an AON alarm, then P1 keeping SRAM bank 0 until GPIO 14 rises (driven from outside by the test), with BOOTDIS | each boot's STATE, LAST_SWCORE_PWRUP and HAD_SWCORE_PD, the return codes, the SRAM word lost and kept, BOOTDIS consumed (`chip/power_tests.sysl`) |
| `xip_sram.c` | the whole program in the XIP cache's 16 KB (`xip_ram`), as the SDK's `pico_xip_sram_test` | `main` and the stack at 0x13ffc000 on, five lines 100 ms apart, `PASSED` |
| `xip_loop.c` (and `sram_loop`, the same source built for SRAM) | 20000 rounds of a sort and a checksum with no waiting, from XIP_SRAM and from SRAM, for measuring the two paths side by side | `checksum d0a54a76`, `PASSED` |
| `stack_limit.c` | MSPLIM 1 KB below SP, then a 2 KB frame: the UsageFault STKOF in its own handler, which lifts the limit so the `SUB` runs again | its report: one fault, CFSR's STKOF, the faulting PC in `deep` |
| `tt.c` | ACLE's `cmse_TT`/`TTT`/`TTA`/`TTAT` of SRAM and the PPB, before and after two SAU regions (built with `-mcmse`) | each answer's word |
| `nonsecure.c` (with `nonsecure_ns.S`) | SAU regions for its own Non-secure code, stack and SG veneer; a `cmse_nonsecure_call` (BLXNS) to Non-secure code that calls back through the veneer and returns by FNC_RETURN; TT of the three; a Non-secure load of Secure memory, SecureFault AUVIOL in its handler | the result and the call count, the TT words, SFSR and SFAR |
| `rcp.c` | every `hardware/rcp.h` function and macro, both forms, passing; then a corrupted canary | its lines on UART0, then core 0 stopped at `Fault` with `rcp_canary_check` named |
| `gpioc.c` | every `hardware/gpio_coproc.h` function, each compared with its SIO equivalent | `87 checks, 0 mismatches` and the final OUT and OE |
| `dcp.c` (+ `dcp_ops.S`) | the double coprocessor: pico_double's arithmetic, `sqrt`, `fma`, comparisons, casts, `ddiv_fast`, `sqrt_fast`, and every canned sequence of `hardware/dcp_canned.inc.S`, over 3000 pseudo-random operand sets, one hash per operation; core 1 adding, multiplying and dividing alongside; a 20 µs timer interrupting with double arithmetic of its own; then newlib's `printf` `%f`, `%e`, `%g` | each hash equal to the host's IEEE arithmetic (denormals flushed), the interrupts' count and check, and the printed numbers digit for digit |
| `interp.c` | `hardware_interp` in each mode (counter, blend unsigned and signed, clamp, rotate, FORCE_MSB, ADD_RAW, cross, save/restore); core 1 counting on its own INTERP0 at the same time; the multicore doorbells, each core's SIO_IRQ_BELL handler acknowledging | each line's values, worked from the datasheet (`chip/interp_tests.sysl`) |
| `usb_device.c` | the SDK's stdio_usb (TinyUSB's CDC device and the reset interface) beside UART0: waits to be configured, then for DTR, prints a line and reads a byte | the descriptors a host enumerating it reads, equal to the ELF's own; the line on bulk IN, the byte from bulk OUT (`chip/usb_tests.sysl`); benchtop's own host opening its port, and the same through a real pty (`chip/usb_host_tests.sysl`, `host/pty_tests.sysl`) |
| `usb_reconnect.c` | stdio_usb taken off the bus with `tud_disconnect` and put back with `tud_connect` | `first` and `second` over USB, the device enumerated twice by benchtop's host (`chip/usb_host_tests.sysl`) |
| `hello_usb.c` | pico-examples' `hello_world/usb` (commit 0d62f75), unchanged: a flash image, stdio over USB only, `Hello, world!` every second | the lines from the second second on, over benchtop's host to the console (`chip/usb_host_tests.sysl`, `host/pty_tests.sysl` through `benchtop run`) |
| `dev_lowlevel.c` (+ `dev_lowlevel.h`, `usb_common.h`) | pico-examples' `usb/device/dev_lowlevel` (commit 0d62f75), unchanged: a vendor-class device driven straight from the USB registers, stdio on UART0 | its enumeration up to the configuration, then benchtop's host stopping on its class by name (`chip/usb_host_tests.sysl`) |

| `rom.c` | Raspberry Pi's boot ROM called as the SDK calls it: its magic, version and git revision, `rom_func_lookup` of ten codes and one it lacks, `get_sys_info`'s BOOT_INFO, a sector erased and a page programmed 1 MB into the flash and the flash's unique ID read through the ROM's flash functions (`hardware_flash`), then a PC/SP reboot through the ROM's `reboot` into a function that sets the clocks up again (the reboot put CLOCKS back to power-on, clk_peri off) and reads BOOT_INFO again | every line printed over UART0, the programmed bytes in the flash, the salts kept over the reboot (`chip/arm_tests.sysl`) |
| `otp.c` | the OTP through the ROM: the chip ID through OTP_DATA, `get_sys_info` (CHIP_INFO, CRITICAL, CPU_INFO) and `otp_access`; user rows written with ECC and read back through the ROM, the ECC window and the raw window; a raw row; a write that would clear a bit; each data bit's codeword; an ECC write over a set bit; pages locked read-only and inaccessible by SW_LOCK; the ROM's argument checks | every line, the rows the ROM programmed, and each codeword against benchtop's own ECC (`chip/otp_tests.sysl`) |

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
- stdio on UART0, not USB (`usb_device` has both);
- `PICO_CRT0_DEBUG_ENTRY_RESETS_VIA_BOOTROM=0`. By default an RP2350 RAM image entered by a debugger
  goes back through the boot ROM's RAM-image boot (watchdog scratch registers, then the ROM's vector
  table) to be launched again; benchtop runs the Raspberry Pi ROM's functions and core 1's wait in
  it, but not core 0's boot path through it (`docs/rp-coverage.md` item 30), so the image instead
  initialises the redundancy coprocessor and MSPLIM itself and vectors through its own table, the
  SDK's other supported entry.
