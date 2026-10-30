// benchtop's boot ROM for an RP2350 on its Cortex-M33s: the Arm half of the ROM's documented
// interface (datasheet 5.4, "Bootrom APIs") that the SDK's start-up reaches, written for benchtop and
// built by build.sh into rom.bin, which `ArmBoard` maps read-only at address 0. It is not the
// Raspberry Pi ROM: only the well-known locations, the lookup, and the functions in the table below
// exist. A lookup of any other code answers 0 (not found), as the real ROM does for a code it lacks.
//
// The well-known locations: the vector table at 0x00, the magic 'M' 'u' 0x02 and the version at
// 0x10, the function table's address at 0x14, and the lookup function's address (with its Thumb bit)
// at 0x16.

    .syntax unified
    .cpu cortex-m33
    .thumb

    .section .text
    .org 0x00
vectors:
    .word 0x20082000            // initial SP: the top of SRAM
    .word park + 1              // reset: core 1 waits here (benchtop runs the launch protocol itself)

    .org 0x10
    .byte 'M', 'u', 0x02, 0x02  // magic, version 2
    .hword func_table           // BOOTROM_FUNC_TABLE_OFFSET
    .hword lookup + 1           // BOOTROM_TABLE_LOOKUP_OFFSET

    .org 0x40
    .thumb_func
park:
    wfe
    b park

// void *rom_table_lookup(uint32_t code, uint32_t mask): the first entry whose code is `code` and
// whose flags share a bit with `mask`, or 0.
    .thumb_func
lookup:
    adr r2, func_table
1:
    ldrh r3, [r2]
    cbz r3, 3f
    cmp r3, r0
    bne 2f
    ldrh r3, [r2, #2]
    tst r3, r1
    beq 2f
    ldrh r0, [r2, #4]
    bx lr
2:
    adds r2, #6
    b 1b
3:
    movs r0, #0
    bx lr

// int bootrom_state_reset(uint32_t flags): benchtop's ROM keeps no state of its own (no boot locks
// taken for the caller, no stack guard, no saved permissions), so there is nothing to reset.
    .thumb_func
state_reset:
    bx lr

// int get_sys_info(uint32_t *out, uint32_t words, uint32_t flags): of the SYS_INFO_* flags only
// BOOT_INFO (0x40) is answered, with the four words the board keeps at boot_info. out[0] is the flags
// answered, then each one's words; the answer is the number of words written, or
// BOOTROM_ERROR_BUFFER_TOO_SMALL (-13) where `out` cannot hold them.
    .thumb_func
get_sys_info:
    cbz r1, 2f
    ands r2, r2, #0x40
    str r2, [r0]
    cbz r2, 1f
    cmp r1, #5
    blo 2f
    adr r3, boot_info
    ldmia r3, {r1, r2, r3, r12}
    adds r0, #4
    stmia r0, {r1, r2, r3, r12}
    movs r0, #5
    bx lr
1:
    movs r0, #1
    bx lr
2:
    mvn r0, #12
    bx lr

// The function table: code ('S' | 'R' << 8 as ROM_TABLE_CODE makes it), the RT_FLAG_FUNC_* flags it
// answers for, and the function's address with its Thumb bit; a zero code ends it.
    .align 2
func_table:
    .hword 'S' | ('R' << 8), 0x0004 | 0x0010, state_reset + 1
    .hword 'G' | ('S' << 8), 0x0004 | 0x0010, get_sys_info + 1
    .hword 0, 0, 0

// BOOT_INFO: the boot word (diagnostic partition, boot type, partition, TBYB and update flags), the
// boot diagnostic and the two reboot parameters. The board writes them at each boot (`ArmBoard`,
// BOOT_INFO_AT); the ROM is read-only to the cores.
    .org 0x100
boot_info:
    .word 0, 0, 0, 0
