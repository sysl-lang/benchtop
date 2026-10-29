// The redundancy coprocessor through the SDK's hardware/rcp.h: every instruction it emits, in both
// forms, with operands that pass; then a corrupted canary, which the RCP faults on.

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/rcp.h"

#define MASK 0x12345678u

int main(void) {
    stdio_init_all();
    // crt0 set both salts on the way in.
    printf("salt %s\n", rcp_is_true(rcp_canary_status()) && rcp_is_true(rcp_canary_status_nodelay()) ? "set" : "unset");

    uint32_t a = rcp_canary_get(0x5a);
    uint32_t b = rcp_canary_get_nodelay(0x5a);
    uint32_t c = rcp_canary_get(0xa5);
    printf("canary %s, tags %s\n", a == b ? "stable" : "unstable", a != c ? "differ" : "collide");
    rcp_canary_check(0x5a, a);
    rcp_canary_check_nodelay(0xa5, c);

    rcp_bvalid(RCP_MASK_TRUE);
    rcp_bvalid_nodelay(RCP_MASK_FALSE);
    rcp_btrue(RCP_MASK_TRUE);
    rcp_btrue_nodelay(RCP_MASK_TRUE);
    rcp_bfalse(RCP_MASK_FALSE);
    rcp_bfalse_nodelay(RCP_MASK_FALSE);
    rcp_b2valid(RCP_MASK_TRUE, RCP_MASK_FALSE);
    rcp_b2valid_nodelay(RCP_MASK_FALSE, RCP_MASK_FALSE);
    rcp_b2and(RCP_MASK_TRUE, RCP_MASK_TRUE);
    rcp_b2and_nodelay(RCP_MASK_TRUE, RCP_MASK_TRUE);
    rcp_b2or(RCP_MASK_FALSE, RCP_MASK_TRUE);
    rcp_b2or_nodelay(RCP_MASK_TRUE, RCP_MASK_FALSE);
    rcp_bxorvalid(RCP_MASK_FALSE ^ MASK, MASK);
    rcp_bxorvalid_nodelay(RCP_MASK_TRUE ^ MASK, MASK);
    rcp_bxortrue(RCP_MASK_TRUE ^ MASK, MASK);
    rcp_bxortrue_nodelay(RCP_MASK_TRUE ^ MASK, MASK);
    rcp_bxorfalse(RCP_MASK_FALSE ^ MASK, MASK);
    rcp_bxorfalse_nodelay(RCP_MASK_FALSE ^ MASK, MASK);
    rcp_ivalid(1234, 1234 ^ RCP_MASK_INTXOR);
    rcp_ivalid_nodelay(MASK, MASK ^ RCP_MASK_INTXOR);
    rcp_iequal(42, 42);
    rcp_iequal_nodelay(MASK, MASK);
    printf("booleans and integers pass\n");

    rcp_count_set(3);
    rcp_count_check(3);
    rcp_count_check_nodelay(4);
    rcp_count_set_nodelay(0xff);
    rcp_count_check(0xff);
    rcp_count_check(0);
    printf("sequence counts pass\n");

    uint8_t r = rcp_random_byte();
    r ^= rcp_random_byte_nodelay();
    printf("random bytes read\n");

    // A stack canary overwritten: the RCP stops the core here.
    printf("checking a corrupted canary\n");
    uart_tx_wait_blocking(uart0);
    rcp_canary_check(0x5a, a ^ (1u | r << 1));
    printf("not reached\n");
    return 0;
}
