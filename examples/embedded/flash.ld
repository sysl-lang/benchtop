ENTRY(start)
MEMORY {
  FLASH (rx) : ORIGIN = 0x10000000, LENGTH = 4M
  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = DEFINED(__ram_size) ? __ram_size : 1M
}
PHDRS { code PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); zero PT_LOAD FLAGS(6); }
SECTIONS {
  .vectors : { KEEP(*(.rodata.vectors)) } > FLASH :code
  .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH :code
  .ARM.extab : { *(.ARM.extab*) } > FLASH :code
  .ARM.exidx : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } > FLASH :code
  .data : {
    . = ALIGN(4); __data_start = .;
    *(.data*)
    . = ALIGN(4); __data_end = .;
  } > RAM AT> FLASH :data
  __data_load = LOADADDR(.data);
  .bss (NOLOAD) : {
    . = ALIGN(4); __bss_start = .;
    *(.bss*) *(COMMON)
    . = ALIGN(4); __bss_end = .;
  } > RAM :zero
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
