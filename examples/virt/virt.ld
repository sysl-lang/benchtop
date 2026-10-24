/* The virtual board: 1 MB of RAM from address 0 holds everything, the vector table first. */

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
    .bss     : { __bss_start = .; *(.bss*) *(COMMON); __bss_end = .; } > RAM

    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
