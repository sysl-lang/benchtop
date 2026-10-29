/* A RAM image for the RP2350: code and data in SRAM0-7, core 0's stack at the top of them, core 1's in
   SCRATCH_X. */
ENTRY(_start)
MEMORY {
    SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 512K
    SCRATCH_X (rw) : ORIGIN = 0x20080000, LENGTH = 4K
}
SECTIONS {
    .text : { *(.text.start) *(.text .text.*) } > SRAM
    .rodata : { *(.rodata .rodata.*) *(.srodata .srodata.*) } > SRAM
    .data : { *(.data .data.*) *(.sdata .sdata.*) } > SRAM
    .bss (NOLOAD) : { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) } > SRAM
    __stack_top = ORIGIN(SRAM) + LENGTH(SRAM);
    __core1_stack_top = ORIGIN(SCRATCH_X) + LENGTH(SCRATCH_X);
    /DISCARD/ : { *(.comment) *(.riscv.attributes) }
}
