ENTRY(start)
MEMORY { RAM (rwx) : ORIGIN = 0, LENGTH = 1M }
SECTIONS {
  .vectors : { KEEP(*(.rodata.vectors)) } > RAM
  .text : { *(.text*) *(.rodata*) } > RAM
  .data : { *(.data*) } > RAM
  .bss : { *(.bss*) *(COMMON) } > RAM
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
