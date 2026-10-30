// The Non-secure half of nonsecure.c: Non-secure code, its stack, and the Non-secure-callable veneer
// into Secure state. Each block starts and ends on a 32-byte boundary, the SAU's granule, inside an
// input section the SDK's linker script already places, so the Secure code can make each exactly one
// SAU region from its start and end labels.

    .syntax unified
    .thumb

// Non-secure code.
    .section .text.ns_text, "ax"
    .balign 32
    .global ns_text_start, ns_text_end
ns_text_start:

// int ns_function(int x): returns secure_double(x + 1) + 100, calling Secure state through the
// veneer. Entered by BLXNS, so LR holds FNC_RETURN, which the POP branches to.
    .global ns_function
    .thumb_func
ns_function:
    push {r4, lr}
    adds r0, r0, #1
    ldr r4, =secure_double_veneer
    blx r4
    adds r0, r0, #100
    pop {r4, pc}

// int ns_peek(const int *p): a Non-secure load from `p`.
    .global ns_peek
    .thumb_func
ns_peek:
    ldr r0, [r0]
    bx lr

    .ltorg
    .balign 32
ns_text_end:

// The Non-secure stack.
    .section .data.ns_data, "aw"
    .balign 32
    .global ns_data_start, ns_data_end, ns_stack_top
ns_data_start:
    .space 256
ns_stack_top:
ns_data_end:

// The veneer: SG, then on to the Secure entry.
    .section .text.nsc_veneers, "ax"
    .balign 32
    .global nsc_start, nsc_end, secure_double_veneer
nsc_start:
    .thumb_func
secure_double_veneer:
    sg
    b.w secure_double_entry
    .balign 32
nsc_end:

// The Secure entry, in Secure memory: secure_double, then back to the Non-secure caller with BXNS, the
// caller-saved registers it would otherwise see cleared. (The compiler's cmse_nonsecure_entry makes the
// same, but asks the linker for veneers of its own in a section the SDK's script does not place.)
    .section .text.secure_entry, "ax"
    .thumb_func
secure_double_entry:
    push {r4, lr}
    bl secure_double
    pop {r4, lr}
    movs r1, #0
    movs r2, #0
    movs r3, #0
    mov r12, r1
    bxns lr
