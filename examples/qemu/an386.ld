/* QEMU's mps2-an386 (Cortex-M4) and mps2-an500 (Cortex-M7): the vector table at 0, in the first
   SSRAM, where code, data and the stack all sit in its first megabyte. */

MEMORY
{
    RAM (rwx) : ORIGIN = 0x00000000, LENGTH = 1M
}

ENTRY(reset)

SECTIONS
{
    .vectors : { KEEP(*(.vectors)) } > RAM
    .text    : { *(.text*) *(.rodata*) } > RAM
    .data    : { *(.data*) } > RAM
    .bss     : { *(.bss*) *(COMMON) } > RAM

    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
