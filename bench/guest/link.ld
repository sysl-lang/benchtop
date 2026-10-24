SECTIONS {
  . = 0;
  .text : { *(.text.start) *(.text*) *(.rodata*) }
  .data : { *(.data*) }
  .bss  : { *(.bss*) *(COMMON) }
}
