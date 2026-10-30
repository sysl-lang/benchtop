# The boot ROMs

## RP2040: benchtop's own

`rp2040/rom.bin` is benchtop's own boot ROM for the RP2040, 16 KB, which the `pico` board maps at 0
(`../rp2040.sysl`). It is written in sysl to work as the RP2040's does, from the datasheet-level
facts in the Pico SDK (`pico/bootrom.h`, `bootrom_constants.h`, `pico/bootrom/sf_table.h`, the
`pico_float` and `pico_double` callers, `boot_stage2`, `hardware_regs`) and the SDK's documented
behaviour; no part of Raspberry Pi's image or its float code was read or copied. ISC, as benchtop.

- `rp2040/sh/sysl/rp2040rom/`: the routines, a sysl package. The soft float and double tables
  (`float32.sysl`, `float64.sysl`, `round.sysl`, `xf.sysl`, `sci.sysl`, `tables.sysl`), the bit and
  memory functions (`bits.sysl`); and `oracle/`, host code that checks them against the host's IEEE
  arithmetic (`../rom40_float_tests.sysl`, `rp2040/examples/sweep`).
- `rp2040/image/`: the image. The vectors, the header and the tables' placement (`main.sysl`,
  `rom.ld`), core 0's boot path and core 1's launch wait (`boot.sysl`), the flash functions
  (`flash.sysl`), and the instructions no statement produces (`hw.sysl`).
- **Build:** `rp2040/image/build.sh` (sysl, Arm's `arm-none-eabi` binutils); it prints the image's
  SHA-256, and the same sources give the same bytes.

## RP2350

`bootrom-rp2350-a4.bin` is Raspberry Pi's boot ROM for the RP2350's A4 stepping, all 32 KB of it,
unmodified. Both RP2350 boards map it at address 0 (`../bootrom.sysl`).

- **Upstream:** <https://github.com/raspberrypi/pico-bootrom-rp2350>, tag `A4`, commit
  `c6cdb1711f32c3e34faaebd58618a6d096dbd52e`.
- **The file:** the `A4` release's asset `bootrom-combined.bin`, the image the repository's
  `make-combined-bootrom.sh` produces from its Arm, NS-boot and RISC-V builds.
- **SHA-256:** `c5f9c48c6539f043c423344a53ceaea3774dd1e4c337af49da741a5d259c84bc`
- **Why the release's image and not a local build:** the README upstream requires GCC 12.2 for the
  Arm halves and the CORE-V RISC-V GCC 12 for the RISC-V half ("because space is very tight, slight
  compiler variations can cause code not to fit"); neither is on this machine.
- **License:** BSD-3-Clause, `LICENSE.TXT` beside it, copied from the same commit.

The release's ELFs, disassemblies and link maps (`arm-bootrom.elf`, `riscv-bootrom.elf`,
`nsboot.elf` and their `.dis` and `.map`) are not kept here; the addresses benchtop relies on are
named where they are used.
