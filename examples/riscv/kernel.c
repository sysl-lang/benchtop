// The same defined unsigned arithmetic is compiled for the host and all RISC-V profiles.
// RISC-V has neither libc nor a runtime here; start.S sets the stack and captures a0 at EBREAK.
#include <stdint.h>
#ifdef HOST
#include <stdio.h>
#endif

uint32_t kernel(void) {
    uint32_t words[32];
    uint32_t state = 0x81234567u;
    for (unsigned i = 0; i < 32; ++i) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        words[i] = state;
    }
    // Insertion sort exercises stack accesses, unsigned comparisons and backward branches.
    for (unsigned i = 1; i < 32; ++i) {
        uint32_t word = words[i];
        unsigned j = i;
        while (j && words[j - 1] > word) {
            words[j] = words[j - 1];
            --j;
        }
        words[j] = word;
    }
    uint32_t hash = 0;
    for (unsigned i = 0; i < 32; ++i)
        hash = ((hash << 7) | (hash >> 25)) ^ words[i];
    return hash;
}
#ifdef HOST
int main(void) { printf("%u\n", kernel()); }
#endif
