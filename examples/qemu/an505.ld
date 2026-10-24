/* QEMU's mps2-an505: its Cortex-M33 boots in Secure state with the vector table at 0x10000000, the
   Secure alias of the first SSRAM. Code, data and the stack all sit in its first megabyte. */

MEMORY
{
    RAM (rwx) : ORIGIN = 0x10000000, LENGTH = 1M
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
