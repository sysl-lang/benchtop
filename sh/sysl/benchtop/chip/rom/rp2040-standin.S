// benchtop's stand-in for the RP2040's boot ROM, until Raspberry Pi's own image is mapped
// (docs/rp-coverage.md item 23). It is not a ROM of Raspberry Pi's and does not pretend to be one: it
// carries the header the SDK reads, the cores' reset vector, and core 1's wait for a launch, and
// nothing else. `build-rp2040-standin.sh` assembles it into `rp2040-standin.bin`.
//
// **The header** (RP2040 datasheet 2.8.3, "Bootrom Contents"): the magic 'M', 'u', 1 and the version
// byte at 0x10, then the three 16-bit pointers at 0x14, 0x16 and 0x18 that `rom_func_lookup` follows:
// the function table, the data table and `rom_table_lookup`. All three point past the first 4 KB, which
// is all of this image the board maps; the chip refuses an access there by name (`Rp2040.rom_refusal`),
// so a program that looks a function up stops at the lookup, named, rather than running on with a wrong
// answer. So does an exception taken while a core is in here (NMI and HardFault), and core 0 starting
// here at all, which the real ROM answers with its boot path (boot2 from flash, or USB).
//
// **Core 1's wait** is the RP2040 ROM's protocol as the datasheet gives it (2.8.2, "Launching Code On
// Processor Core 1") and the SDK's `multicore_launch_core1_raw` drives it: core 1 drains its FIFO, sends
// core 0 a 0 (which `multicore_reset_core1`'s caller drains), then echoes every word it reads, following
// the sequence 0, 0, 1, VTOR, SP, ENTRY. Zeros at the start keep its place; any other word out of place
// starts it again. After the echo of ENTRY it writes VTOR, takes SP as MSP and branches to ENTRY. It
// sleeps in WFE while its FIFO is empty or core 0's is full, and SEVs after each word it sends, as the
// SDK's own FIFO functions do.

    .syntax unified
    .cpu cortex-m0plus
    .thumb

    .equ SIO_BASE, 0xd0000000
    .equ SIO_CPUID, 0x000
    .equ SIO_FIFO_ST, 0x050
    .equ SIO_FIFO_WR, 0x054
    .equ SIO_FIFO_RD, 0x058
    .equ VTOR, 0xe000ed08

    // What the chip refuses by name (`Rp2040.rom_refusal`); none of it is in the mapped 4 KB.
    .equ FUNC_TABLE, 0x1000
    .equ DATA_TABLE, 0x1100
    .equ TABLE_LOOKUP, 0x1200
    .equ CORE0_BOOT, 0x1300
    .equ NMI, 0x1400
    .equ HARD_FAULT, 0x1500

    .section .text
    .global _start
_start:
    .word 0x20042000
    .word reset + 1
    .word NMI + 1
    .word HARD_FAULT + 1
    .byte 'M', 'u', 1, 3
    .hword FUNC_TABLE, DATA_TABLE, TABLE_LOOKUP + 1

    .thumb_func
reset:
    ldr r0, =SIO_BASE
    ldr r1, [r0, #SIO_CPUID]
    cmp r1, #0
    bne core1
    ldr r1, =CORE0_BOOT + 1
    bx r1

core1:
drain:
    ldr r1, [r0, #SIO_FIFO_ST]
    lsrs r1, r1, #1
    bcc drained
    ldr r1, [r0, #SIO_FIFO_RD]
    b drain
drained:
    movs r1, #0
    bl push
restart:
    movs r4, #0
next:
    bl pop
    bl push
    cmp r4, #2
    blo zeros
    beq one
    cmp r4, #3
    beq take_vtor
    cmp r4, #4
    beq take_sp
    // The echo of ENTRY: launch.
    ldr r2, =VTOR
    str r5, [r2]
    msr msp, r6
    bx r1

zeros:
    cmp r1, #0
    bne restart
    adds r4, r4, #1
    b next
one:
    cmp r1, #1
    beq one_seen
    cmp r1, #0
    beq next
    b restart
one_seen:
    movs r4, #3
    b next
take_vtor:
    mov r5, r1
    adds r4, r4, #1
    b next
take_sp:
    mov r6, r1
    adds r4, r4, #1
    b next

// A word from core 0, into r1, sleeping until one is there.
    .thumb_func
pop:
    ldr r1, [r0, #SIO_FIFO_ST]
    lsrs r1, r1, #1
    bcs pop_ready
    wfe
    b pop
pop_ready:
    ldr r1, [r0, #SIO_FIFO_RD]
    bx lr

// r1 to core 0, sleeping until its FIFO has room; then SEV for core 0.
    .thumb_func
push:
    ldr r2, [r0, #SIO_FIFO_ST]
    lsrs r2, r2, #2
    bcs push_ready
    wfe
    b push
push_ready:
    str r1, [r0, #SIO_FIFO_WR]
    sev
    bx lr

    .ltorg
