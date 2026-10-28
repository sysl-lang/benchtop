// The RISC-V entry for `kernel-rv.bin`: the stack at the top of 256 KB, `run(1000)`, and EBREAK
// with the answer in a0, which `benchtop bench --core hazard3` checks.
    .section .text.start,"ax",@progbits
    .globl _start
_start:
    li sp, 0x00040000
    li a0, 1000
    call run
    ebreak
1:  j 1b
