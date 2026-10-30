/* benchtop's own RP2040 boot ROM: where each part of the image lies in its 16 KB at address 0.
 *
 * The header's 16-bit pointers and the function and data tables are written here, as SHORT data,
 * because each entry is the low half of a link-time address: no language's constant expressions can
 * produce one (C refuses `(uint16_t)(uintptr_t)&f` as "not a compile-time constant"), and the linker
 * resolves it. A function's entry has its Thumb bit set (`+ 1`); a datum's does not. Everything else,
 * the vectors, the magic, the soft float and double tables and every routine, is sysl (`main.sysl`).
 */

ENTRY(rom_reset)

/* Every symbol the tables name, kept: a linker script's own reference does not keep a section from
 * --gc-sections, and a symbol whose section was dropped reads as 0.
 */
EXTERN(rom_table_lookup rom_popcount32 rom_reverse32 rom_clz32 rom_ctz32 rom_memset rom_memset4
       rom_memcpy rom_memcpy44 rom_reset_usb_boot rom_connect_internal_flash rom_flash_exit_xip
       rom_flash_range_erase rom_flash_range_program rom_flash_flush_cache rom_flash_enter_cmd_xip
       rom_copyright rom_git_revision rom_sf_table rom_sd_table)

MEMORY
{
    ROM (rx) : ORIGIN = 0x00000000, LENGTH = 16K
}

SECTIONS
{
    .text :
    {
        /* 0x00: SP, reset, NMI, HardFault; 0x10: 'M', 'u', 1, the version. */
        KEEP(*(.rom.vectors))
        /* 0x14: the function table, the data table, rom_table_lookup. */
        SHORT(rom_func_table)
        SHORT(rom_data_table)
        SHORT(rom_table_lookup + 1)

        . = ALIGN(4);
        rom_func_table = .;
        SHORT(0x3350) SHORT(rom_popcount32 + 1)              /* 'P', '3' */
        SHORT(0x3352) SHORT(rom_reverse32 + 1)               /* 'R', '3' */
        SHORT(0x334C) SHORT(rom_clz32 + 1)                   /* 'L', '3' */
        SHORT(0x3354) SHORT(rom_ctz32 + 1)                   /* 'T', '3' */
        SHORT(0x534D) SHORT(rom_memset + 1)                  /* 'M', 'S' */
        SHORT(0x3453) SHORT(rom_memset4 + 1)                 /* 'S', '4' */
        SHORT(0x434D) SHORT(rom_memcpy + 1)                  /* 'M', 'C' */
        SHORT(0x3443) SHORT(rom_memcpy44 + 1)                /* 'C', '4' */
        SHORT(0x4255) SHORT(rom_reset_usb_boot + 1)          /* 'U', 'B' */
        SHORT(0x4649) SHORT(rom_connect_internal_flash + 1)  /* 'I', 'F' */
        SHORT(0x5845) SHORT(rom_flash_exit_xip + 1)          /* 'E', 'X' */
        SHORT(0x4552) SHORT(rom_flash_range_erase + 1)       /* 'R', 'E' */
        SHORT(0x5052) SHORT(rom_flash_range_program + 1)     /* 'R', 'P' */
        SHORT(0x4346) SHORT(rom_flash_flush_cache + 1)       /* 'F', 'C' */
        SHORT(0x5843) SHORT(rom_flash_enter_cmd_xip + 1)     /* 'C', 'X' */
        SHORT(0)

        . = ALIGN(4);
        rom_data_table = .;
        SHORT(0x5243) SHORT(rom_copyright)                   /* 'C', 'R' */
        SHORT(0x5247) SHORT(rom_git_revision)                /* 'G', 'R' */
        SHORT(0x4653) SHORT(rom_sf_table)                    /* 'S', 'F' */
        SHORT(0x4453) SHORT(rom_sd_table)                    /* 'S', 'D' */
        SHORT(0)

        /* Each soft float table with its size in words in the byte two before it. The compiler aligns
         * each table, 128 bytes, to 16, so the size goes in the last two bytes before a 16-byte
         * boundary, and the ASSERT below checks that each table follows its size directly.
         */
        . = ALIGN(16) + 14;
        rom_sf_size = .;
        KEEP(*(.rom.sf_size))
        KEEP(*(.rom.sf))
        . = ALIGN(16) + 14;
        rom_sd_size = .;
        KEEP(*(.rom.sd_size))
        KEEP(*(.rom.sd))

        /* Where `reset_usb_boot`'s reboot enters the ROM, at a fixed address the chip names
         * (`ROM_USB_BOOT_RP2040`).
         */
        . = 0x200;
        KEEP(*(.text.usb_boot))

        *(.text*)
        *(.rodata*)
        rom_code_end = .;

        /* The doorbell, the last word: never written by the image. */
        . = 0x3FFC;
        LONG(0)
    } > ROM

    /* A ROM has no writable storage: anything here is a module `var`, or a `val` whose initializer did
     * not fold, that the image reaches.
     */
    .data : { *(.data*) *(.bss*) *(COMMON) } > ROM
    ASSERT(SIZEOF(.data) == 0, "the ROM image has writable storage, which a ROM cannot hold")
    ASSERT(rom_sf_table == rom_sf_size + 2 && rom_sd_table == rom_sd_size + 2, "a soft float table does not follow its size")

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
