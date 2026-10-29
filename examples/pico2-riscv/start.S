// Core 0's entry, as a debugger enters a RAM image: a stack in SRAM, a trap vector, `main`, and
// EBREAK to end the run. A trap of any kind also ends it at EBREAK.
.section .text.start,"ax",@progbits
.globl _start
_start:
    la sp, __stack_top
    la t0, trap
    csrw mtvec, t0
    call main
    ebreak
1:  j 1b

.text
.globl trap
.balign 4
trap:
    ebreak
    j trap
