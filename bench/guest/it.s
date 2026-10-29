@ A guest workload for IT blocks, for `benchtop bench --core cortex-m33`: a loop in which most
@ instructions are inside a block, of every length from one to four, 16- and 32-bit, thens and elses,
@ one whose compare inside the block changes the flags the rest of it tests, and a pseudo-random
@ sequence so that every condition goes both ways. `it.c` computes the same on the host.

    .syntax unified
    .thumb
    .global run
    .text
    .thumb_func
run:                                @ r0 = rounds
    push    {r4-r7, lr}
    movw    r1, #12345              @ x
    movs    r2, #0                  @ acc
    movs    r3, #0                  @ lo
    movs    r4, #0                  @ hi
    ldr     r6, =1103515245
    movw    r12, #12345
outer:
    movw    r5, #10000
inner:
    muls    r1, r6, r1
    add     r1, r12                 @ x = x * 1103515245 + 12345
    lsrs    r7, r1, #16             @ v = x >> 16

    cmp     r7, r3                  @ one: lo = min(lo, v)
    it      lo
    movlo   r3, r7

    cmp     r7, r4                  @ two: hi = max(hi, v), else count
    ite     hi
    movhi   r4, r7
    addls   r2, r2, #1

    tst     r1, #0x100              @ three, mixed widths
    ittt    ne
    addne   r2, r7
    eorne.w r2, r2, r1, ror #7
    addne.w r3, r3, #1

    tst     r1, #0x8000             @ four, then and else alternating
    itete   eq
    subeq   r2, r2, r7
    addne   r2, r2, r7
    eoreq.w r4, r4, #0x55
    rorne.w r2, r2, #3

    cmp     r7, #0x8000             @ a compare inside the block moves the flags the next one tests
    itt     hs
    cmphs   r2, r3
    addhs   r3, r3, #1

    subs    r5, #1
    bne     inner
    subs    r0, #1
    bne     outer

    eors    r2, r3
    eors    r2, r4
    mov     r0, r2
    pop     {r4-r7, pc}
