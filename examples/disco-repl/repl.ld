/* One Cortex-M7 program for the STM32H747I-DISCO: vectors, code and constants in internal flash,
 * data and the stack in AXI SRAM. The CM4 is never released.
 */
ENTRY(start)
MEMORY {
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 1M
  RAM (rwx) : ORIGIN = 0x24000000, LENGTH = 512K
}
PHDRS { code PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); zero PT_LOAD FLAGS(6); }
SECTIONS {
 .vectors : { KEEP(*(.rodata.vectors)) } > FLASH :code
 .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH :code
 .data : { . = ALIGN(4); __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > RAM AT> FLASH :data
 __data_load = LOADADDR(.data);
 .bss (NOLOAD) : { . = ALIGN(4); __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > RAM :zero
 __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
