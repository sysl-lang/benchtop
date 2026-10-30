# The RP2350 boot ROM

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
