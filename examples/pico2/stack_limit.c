// The M33's stack limit: MSPLIM set 1 KB below the stack pointer and the UsageFault enabled with a
// handler of this program's own, then a call to a function whose frame is 2 KB. Its `sub sp` would
// cross the limit, so it is the UsageFault STKOF instead, with nothing of it done. The handler records
// CFSR and the stacked return address (the faulting instruction), lifts the limit and returns: the
// instruction runs again, and the program goes on to report what happened.

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/exception.h"
#include "hardware/structs/scb.h"

static volatile uint32_t fault_cfsr, fault_pc, faults;

// Lifts the limit and records the fault; `frame` is the exception frame on the main stack.
void __attribute__((used)) usage_fault_c(uint32_t *frame) {
    fault_cfsr = scb_hw->cfsr;
    fault_pc = frame[6];
    faults++;
    scb_hw->cfsr = fault_cfsr;
    __asm volatile("msr msplim, %0" : : "r"(0));
}

// The frame is the main stack pointer at entry; LR still holds EXC_RETURN for the C function's return.
static void __attribute__((naked)) usage_fault(void) {
    __asm volatile("mrs r0, msp\n b usage_fault_c\n");
}

static int __attribute__((noinline)) deep(int n) {
    volatile char buf[2048];
    buf[0] = (char)n;
    buf[sizeof buf - 1] = (char)n;
    return buf[0] + buf[sizeof buf - 1];
}

int main(void) {
    stdio_init_all();
    exception_set_exclusive_handler(USAGEFAULT_EXCEPTION, usage_fault);
    scb_hw->shcsr |= M33_SHCSR_USGFAULTENA_BITS;
    printf("msplim 1 KB below sp\n");
    uint32_t sp, limit;
    __asm volatile("mov %0, sp" : "=r"(sp));
    limit = (sp - 1024) & ~7u;
    __asm volatile("msr msplim, %0" : : "r"(limit));
    int r = deep(21);
    uint32_t now;
    __asm volatile("mrs %0, msplim" : "=r"(now));
    printf("deep returned %d after %lu fault(s)\n", r, (unsigned long)faults);
    uint32_t entry = (uint32_t)deep & ~1u;
    printf("cfsr 0x%08lx (STKOF %s), in deep: %s\n", (unsigned long)fault_cfsr,
           (fault_cfsr & M33_CFSR_UFSR_STKOF_BITS) ? "set" : "clear",
           (fault_pc >= entry && fault_pc < entry + 32) ? "yes" : "no");
    printf("msplim now 0x%08lx, cfsr now 0x%08lx\n", (unsigned long)now, (unsigned long)scb_hw->cfsr);
    for (;;) {
        __breakpoint();
    }
}
