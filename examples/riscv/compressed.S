// Each compressed instruction followed by its canonical RV32I equivalent.
.section .text,"ax",@progbits
.globl _start
_start:
    .option rvc
    c.addi4spn a0, sp, 4
    .option norvc
    addi a0, sp, 4
    .option rvc
    c.addi4spn a0, sp, 16
    .option norvc
    addi a0, sp, 16
    .option rvc
    c.addi4spn a0, sp, 64
    .option norvc
    addi a0, sp, 64
    .option rvc
    c.addi4spn a0, sp, 128
    .option norvc
    addi a0, sp, 128
    .option rvc
    c.addi4spn a0, sp, 256
    .option norvc
    addi a0, sp, 256
    .option rvc
    c.addi4spn a0, sp, 512
    .option norvc
    addi a0, sp, 512
    .option rvc
    c.addi4spn a0, sp, 1020
    .option norvc
    addi a0, sp, 1020
    .option rvc
    c.lw a1, 0(a0)
    .option norvc
    lw a1, 0(a0)
    .option rvc
    c.sw a1, 0(a0)
    .option norvc
    sw a1, 0(a0)
    .option rvc
    c.lw a1, 4(a0)
    .option norvc
    lw a1, 4(a0)
    .option rvc
    c.sw a1, 4(a0)
    .option norvc
    sw a1, 4(a0)
    .option rvc
    c.lw a1, 60(a0)
    .option norvc
    lw a1, 60(a0)
    .option rvc
    c.sw a1, 60(a0)
    .option norvc
    sw a1, 60(a0)
    .option rvc
    c.lw a1, 64(a0)
    .option norvc
    lw a1, 64(a0)
    .option rvc
    c.sw a1, 64(a0)
    .option norvc
    sw a1, 64(a0)
    .option rvc
    c.lw a1, 124(a0)
    .option norvc
    lw a1, 124(a0)
    .option rvc
    c.sw a1, 124(a0)
    .option norvc
    sw a1, 124(a0)
    .option rvc
    c.addi a0, -32
    .option norvc
    addi a0, a0, -32
    .option rvc
    c.li a1, -32
    .option norvc
    addi a1, zero, -32
    .option rvc
    c.andi a1, -32
    .option norvc
    andi a1, a1, -32
    .option rvc
    c.addi a0, -1
    .option norvc
    addi a0, a0, -1
    .option rvc
    c.li a1, -1
    .option norvc
    addi a1, zero, -1
    .option rvc
    c.andi a1, -1
    .option norvc
    andi a1, a1, -1
    .option rvc
    c.addi a0, 0
    .option norvc
    addi a0, a0, 0
    .option rvc
    c.li a1, 0
    .option norvc
    addi a1, zero, 0
    .option rvc
    c.andi a1, 0
    .option norvc
    andi a1, a1, 0
    .option rvc
    c.addi a0, 1
    .option norvc
    addi a0, a0, 1
    .option rvc
    c.li a1, 1
    .option norvc
    addi a1, zero, 1
    .option rvc
    c.andi a1, 1
    .option norvc
    andi a1, a1, 1
    .option rvc
    c.addi a0, 31
    .option norvc
    addi a0, a0, 31
    .option rvc
    c.li a1, 31
    .option norvc
    addi a1, zero, 31
    .option rvc
    c.andi a1, 31
    .option norvc
    andi a1, a1, 31
    .option rvc
    c.addi16sp sp, -512
    .option norvc
    addi sp, sp, -512
    .option rvc
    c.addi16sp sp, -16
    .option norvc
    addi sp, sp, -16
    .option rvc
    c.addi16sp sp, 16
    .option norvc
    addi sp, sp, 16
    .option rvc
    c.addi16sp sp, 496
    .option norvc
    addi sp, sp, 496
    .option rvc
    c.lui a0, 1
    .option norvc
    lui a0, 1
    .option rvc
    c.lui a0, 31
    .option norvc
    lui a0, 31
    .option rvc
    c.lui a0, 1048575
    .option norvc
    lui a0, 1048575
    .option rvc
    c.lui a0, 1048544
    .option norvc
    lui a0, 1048544
    .option rvc
    c.srli a0, 1
    .option norvc
    srli a0, a0, 1
    .option rvc
    c.srli a0, 7
    .option norvc
    srli a0, a0, 7
    .option rvc
    c.srli a0, 16
    .option norvc
    srli a0, a0, 16
    .option rvc
    c.srli a0, 31
    .option norvc
    srli a0, a0, 31
    .option rvc
    c.srai a0, 1
    .option norvc
    srai a0, a0, 1
    .option rvc
    c.srai a0, 7
    .option norvc
    srai a0, a0, 7
    .option rvc
    c.srai a0, 16
    .option norvc
    srai a0, a0, 16
    .option rvc
    c.srai a0, 31
    .option norvc
    srai a0, a0, 31
    .option rvc
    c.slli a0, 1
    .option norvc
    slli a0, a0, 1
    .option rvc
    c.slli a0, 7
    .option norvc
    slli a0, a0, 7
    .option rvc
    c.slli a0, 16
    .option norvc
    slli a0, a0, 16
    .option rvc
    c.slli a0, 31
    .option norvc
    slli a0, a0, 31
    .option rvc
    c.sub a0, a1
    .option norvc
    sub a0, a0, a1
    .option rvc
    c.xor a0, a1
    .option norvc
    xor a0, a0, a1
    .option rvc
    c.or a0, a1
    .option norvc
    or a0, a0, a1
    .option rvc
    c.and a0, a1
    .option norvc
    and a0, a0, a1
    .option rvc
    c.j .-2048
    .option norvc
    jal zero, .-2048
    .option rvc
    c.jal .-2048
    .option norvc
    jal ra, .-2048
    .option rvc
    c.j .-1024
    .option norvc
    jal zero, .-1024
    .option rvc
    c.jal .-1024
    .option norvc
    jal ra, .-1024
    .option rvc
    c.j .-64
    .option norvc
    jal zero, .-64
    .option rvc
    c.jal .-64
    .option norvc
    jal ra, .-64
    .option rvc
    c.j .-2
    .option norvc
    jal zero, .-2
    .option rvc
    c.jal .-2
    .option norvc
    jal ra, .-2
    .option rvc
    c.j .+2
    .option norvc
    jal zero, .+2
    .option rvc
    c.jal .+2
    .option norvc
    jal ra, .+2
    .option rvc
    c.j .+4
    .option norvc
    jal zero, .+4
    .option rvc
    c.jal .+4
    .option norvc
    jal ra, .+4
    .option rvc
    c.j .+32
    .option norvc
    jal zero, .+32
    .option rvc
    c.jal .+32
    .option norvc
    jal ra, .+32
    .option rvc
    c.j .+1024
    .option norvc
    jal zero, .+1024
    .option rvc
    c.jal .+1024
    .option norvc
    jal ra, .+1024
    .option rvc
    c.j .+2046
    .option norvc
    jal zero, .+2046
    .option rvc
    c.jal .+2046
    .option norvc
    jal ra, .+2046
    .option rvc
    c.beqz a0, .-256
    .option norvc
    beq a0, zero, .-256
    .option rvc
    c.beqz a0, .-128
    .option norvc
    beq a0, zero, .-128
    .option rvc
    c.beqz a0, .-2
    .option norvc
    beq a0, zero, .-2
    .option rvc
    c.beqz a0, .+2
    .option norvc
    beq a0, zero, .+2
    .option rvc
    c.beqz a0, .+4
    .option norvc
    beq a0, zero, .+4
    .option rvc
    c.beqz a0, .+64
    .option norvc
    beq a0, zero, .+64
    .option rvc
    c.beqz a0, .+254
    .option norvc
    beq a0, zero, .+254
    .option rvc
    c.bnez a0, .-256
    .option norvc
    bne a0, zero, .-256
    .option rvc
    c.bnez a0, .-128
    .option norvc
    bne a0, zero, .-128
    .option rvc
    c.bnez a0, .-2
    .option norvc
    bne a0, zero, .-2
    .option rvc
    c.bnez a0, .+2
    .option norvc
    bne a0, zero, .+2
    .option rvc
    c.bnez a0, .+4
    .option norvc
    bne a0, zero, .+4
    .option rvc
    c.bnez a0, .+64
    .option norvc
    bne a0, zero, .+64
    .option rvc
    c.bnez a0, .+254
    .option norvc
    bne a0, zero, .+254
    .option rvc
    c.lwsp a0, 0(sp)
    .option norvc
    lw a0, 0(sp)
    .option rvc
    c.swsp a0, 0(sp)
    .option norvc
    sw a0, 0(sp)
    .option rvc
    c.lwsp a0, 4(sp)
    .option norvc
    lw a0, 4(sp)
    .option rvc
    c.swsp a0, 4(sp)
    .option norvc
    sw a0, 4(sp)
    .option rvc
    c.lwsp a0, 60(sp)
    .option norvc
    lw a0, 60(sp)
    .option rvc
    c.swsp a0, 60(sp)
    .option norvc
    sw a0, 60(sp)
    .option rvc
    c.lwsp a0, 64(sp)
    .option norvc
    lw a0, 64(sp)
    .option rvc
    c.swsp a0, 64(sp)
    .option norvc
    sw a0, 64(sp)
    .option rvc
    c.lwsp a0, 128(sp)
    .option norvc
    lw a0, 128(sp)
    .option rvc
    c.swsp a0, 128(sp)
    .option norvc
    sw a0, 128(sp)
    .option rvc
    c.lwsp a0, 252(sp)
    .option norvc
    lw a0, 252(sp)
    .option rvc
    c.swsp a0, 252(sp)
    .option norvc
    sw a0, 252(sp)
    .option rvc
    c.jr ra
    .option norvc
    jalr zero, 0(ra)
    .option rvc
    c.jalr ra
    .option norvc
    jalr ra, 0(ra)
    .option rvc
    c.mv ra, a1
    .option norvc
    add ra, zero, a1
    .option rvc
    c.add ra, a1
    .option norvc
    add ra, ra, a1
    .option rvc
    c.jr t0
    .option norvc
    jalr zero, 0(t0)
    .option rvc
    c.jalr t0
    .option norvc
    jalr ra, 0(t0)
    .option rvc
    c.mv t0, a1
    .option norvc
    add t0, zero, a1
    .option rvc
    c.add t0, a1
    .option norvc
    add t0, t0, a1
    .option rvc
    c.jr a0
    .option norvc
    jalr zero, 0(a0)
    .option rvc
    c.jalr a0
    .option norvc
    jalr ra, 0(a0)
    .option rvc
    c.mv a0, a1
    .option norvc
    add a0, zero, a1
    .option rvc
    c.add a0, a1
    .option norvc
    add a0, a0, a1
    .option rvc
    c.jr t6
    .option norvc
    jalr zero, 0(t6)
    .option rvc
    c.jalr t6
    .option norvc
    jalr ra, 0(t6)
    .option rvc
    c.mv t6, a1
    .option norvc
    add t6, zero, a1
    .option rvc
    c.add t6, a1
    .option norvc
    add t6, t6, a1
    .option rvc
    c.ebreak
    .option norvc
    ebreak
    .option rvc
    c.nop
    .option norvc
    addi zero, zero, 0
