.section .text.start,"ax",@progbits
.globl _start
_start:
    li sp, 0x100000
    call kernel
    ebreak
1:  j 1b
