# Flash-linked firmware

The runnable Cortex-M machine supports one RAM region (4 KB to 1 MB) and one
executable flash region (4 KB to 4 MB). Bases and capacities are multiples of
4 KB. Addresses come from the board's chip description; memory regions and
MMIO windows must not overlap.

For on-chip flash:

```hocon
peripherals {
  ram { peripheral = ram, at = 0x20000000, size = 1M }
  program { peripheral = flash, at = 0x10000000, size = 4M }
}
```

For external flash, the chip describes its mapped interface and the board
supplies a storage part, as in `boards/workbench.hocon`:

```hocon
// In mcu.peripherals:
flash0 { peripheral = flash-interface, at = 0x10000000, size = 4M }
// In board.parts:
firmware { part = W25Q32JV, ref = U3, interface = flash0 }
// In board.buses:
flash0 {}
```

The generic external `flash` part takes `capacity` in bytes. `W25Q32JV` fixes
capacity to 4194304 bytes. Its capacity must equal the interface's mapped size.
Both catalog forms share the same executable, read-only storage engine.

ELF PT_LOAD segments load at physical addresses into RAM or flash. The loader
supports different load and runtime addresses: `.data` initial values stay in
flash until guest startup copies them into RAM. RAM starts at zero and unloaded
flash bytes are 0xff. Raw binaries load at the flash base if flash is configured,
otherwise at the RAM base. The first two words there are the Cortex-M stack and
reset vectors. Boot sets VTOR to that region; reset restores the loaded image.

Both sysl demos now use `examples/embedded/flash.ld`. Their reset functions copy
`.data` and clear `.bss` before using globals. Rebuild with their usual build.sh.
The matrix fixture includes actual initialized and zero-initialized globals;
tests poison them before startup to verify initialization is performed by the
guest rather than accidentally supplied by the loader.

Ordinary CPU writes to flash fault and leave storage intact. Flash controller
registers, serial QSPI transactions, erase/program commands, boot ROMs, security
metadata, UF2 packaging, and hardware programming are not implemented here.
The demos have a flash-linked embedded layout, but still target Benchtop's
virtual MCU peripherals. They are not binaries for a physical Pico.

The RAM-only descriptions and older architectural test images remain supported.
`bench/guest/workbench-ram.hocon` and `panel-ram.elf` retain the earlier RAM-only
workbench for low-level tests that install vectors and instructions at address 0.
