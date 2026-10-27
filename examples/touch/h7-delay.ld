/* M7/M4 core bring-up harness, internal flash and AXI SRAM addresses.
 * BCM4 option byte is disabled; M7 releases CM4 through RCC_GCR.
 * Each core has its own stack; M7 initializes the shared runtime once.
 */
ENTRY(m7_start)
MEMORY {
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 1M
  CM4 (rx) : ORIGIN = 0x08100000, LENGTH = 1M
  AUDIO (rwx) : ORIGIN = 0x30000000, LENGTH = 128K
  RAM (rwx) : ORIGIN = 0x24000000, LENGTH = 512K
}
PHDRS { code PT_LOAD FLAGS(5); cm4 PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); zero PT_LOAD FLAGS(6); audio PT_LOAD FLAGS(6); }
SECTIONS {
 .vectors : { KEEP(*(.rodata.vectors)) } > FLASH :code
 .m4_vectors : { KEEP(*(.rodata.m4_vectors)) } > CM4 :cm4
 .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH :code
 .ARM.extab : { *(.ARM.extab*) } > FLASH :code
 .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH :code
 .data : { . = ALIGN(4); __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > RAM AT> FLASH :data
 __data_load = LOADADDR(.data);
 .bss (NOLOAD) : { . = ALIGN(4); __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > RAM :zero
 .audio_delay (NOLOAD) : { *(.audio_delay) } > AUDIO :audio
 __m7_stack_top = ORIGIN(RAM) + LENGTH(RAM);
 __stack_top = __m7_stack_top - 4096;
 ASSERT(__bss_end < __stack_top - 8192, "M4 stack must fit above shared runtime")
}
