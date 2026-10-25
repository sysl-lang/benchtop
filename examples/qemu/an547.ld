/* QEMU mps3-an547: use the first 64 KB of the Secure ITCM alias for this small oracle. */
MEMORY
{
    RAM (rwx) : ORIGIN = 0x10000000, LENGTH = 64K
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
