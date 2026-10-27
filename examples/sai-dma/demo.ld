ENTRY(start)
MEMORY {
 FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 2M
 RAM (rwx) : ORIGIN = 0x24000000, LENGTH = 512K
}
PHDRS { code PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); zero PT_LOAD FLAGS(6); }
SECTIONS {
 .vectors : { KEEP(*(.rodata.vectors)) } > FLASH :code
 .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH :code
 .ARM.extab : { *(.ARM.extab*) } > FLASH :code
 .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH :code
 .data : { *(.data*) } > RAM AT> FLASH :data
 .bss (NOLOAD) : { *(.bss*) *(COMMON) . = ALIGN(4); } > RAM :zero
 __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
