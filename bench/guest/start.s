    .syntax unified
    .thumb
    .global _start
    .section .text.start
    .thumb_func
_start:
    ldr  r0, =0x00040000
    mov  sp, r0
    ldr  r0, rounds
    bl   run
    bkpt #0xAB
    .align 2
rounds:
    .word 1000
