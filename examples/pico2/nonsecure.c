// TrustZone on the M33: a Secure image (as every SDK image is) that makes Non-secure code of its own.
// It gives the SAU three regions — the Non-secure code, the Non-secure stack, and a Non-secure-callable
// veneer (nonsecure_ns.S) — sets MSP_NS, and calls the Non-secure function through a
// cmse_nonsecure_call pointer (BLXNS). That function calls back into Secure state through the veneer
// (SG), whose Secure entry returns with BXNS, and returns with FNC_RETURN. Then
// TT reports the three regions, and a Non-secure load from Secure memory takes SecureFault AUVIOL,
// whose handler prints SFSR and SFAR (ARMv8-M ARM, "Security attribution", "SecureFault").

#include <arm_cmse.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/exception.h"
#include "hardware/structs/m33.h"
#include "hardware/structs/sau.h"
#include "hardware/sync.h"

typedef int __attribute__((cmse_nonsecure_call)) ns_call_t(int);
typedef int __attribute__((cmse_nonsecure_call)) ns_peek_t(const int *);

extern int ns_function(int x);
extern int ns_peek(const int *p);
extern char ns_stack_top[];
extern char ns_text_start[], ns_text_end[], ns_data_start[], ns_data_end[], nsc_start[], nsc_end[];

static int calls;

// The Secure function the Non-secure code calls back through the veneer: twice its argument.
int secure_double(int x) {
    calls++;
    return 2 * x;
}

// SAU region `k` over [from, to), Non-secure callable where `nsc`.
static void region(unsigned k, char *from, char *to, bool nsc) {
    sau_hw->rnr = k;
    sau_hw->rbar = (uint32_t)from;
    sau_hw->rlar = (((uint32_t)to - 1) & ~0x1fu) | (nsc ? 3 : 1);
}

static void secure_fault(void) {
    printf("securefault sfsr %08lx sfar %08lx\n", (unsigned long)m33_hw->sfsr, (unsigned long)m33_hw->sfar);
    for (;;) {
        __breakpoint();
    }
}

int main(void) {
    stdio_init_all();
    region(0, ns_text_start, ns_text_end, false);
    region(1, ns_data_start, ns_data_end, false);
    region(2, nsc_start, nsc_end, true);
    sau_hw->ctrl = 1;
    __dsb();
    __isb();
    __asm volatile("msr msp_ns, %0" : : "r"(ns_stack_top));

    ns_call_t *call = cmse_nsfptr_create((ns_call_t *)ns_function);
    int r = call(20);
    printf("ns_function(20) = %d after %d call(s)\n", r, calls);
    printf("tt ns %08lx nsc %08lx secure %08lx\n", (unsigned long)cmse_TT((void *)ns_function).value,
           (unsigned long)cmse_TT((void *)nsc_start).value, (unsigned long)cmse_TT((void *)main).value);

    exception_set_exclusive_handler(SECUREFAULT_EXCEPTION, secure_fault);
    m33_hw->shcsr |= 1u << 19;
    ns_peek_t *peek = cmse_nsfptr_create((ns_peek_t *)ns_peek);
    peek(&calls);
    printf("not reached\n");
    for (;;) {
        __breakpoint();
    }
}
