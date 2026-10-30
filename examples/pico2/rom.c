// Raspberry Pi's RP2350 boot ROM, called as the SDK calls it: its header, its function table through
// `rom_func_lookup` (the codes found, one that is not), `rom_get_sys_info`'s BOOT_INFO, the flash
// through the ROM's flash functions (`hardware_flash`:
// connect, exit XIP, erase, program, flush, and the unique ID), and a PC/SP reboot through the ROM's
// `reboot`, after which the function rebooted into reads BOOT_INFO again.
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/runtime_init.h"
#include "hardware/flash.h"
#include "hardware/resets.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

#include "boot/picoboot_constants.h"

#define FLASH_AT (1024 * 1024)

static uint8_t page[FLASH_PAGE_SIZE];
static uint32_t stack[256];

static void print_boot_info(const char *when) {
    uint32_t info[5] = {0};
    int words = rom_get_sys_info(info, 5, SYS_INFO_BOOT_INFO);
    printf("%s: get_sys_info %d words, flags 0x%02lx, boot word 0x%08lx, diagnostic 0x%08lx, params 0x%lx 0x%lx\n",
           when, words, (unsigned long)info[0], (unsigned long)info[1], (unsigned long)info[2], (unsigned long)info[3],
           (unsigned long)info[4]);
}

// Where the ROM's PC/SP reboot lands, on the stack it was given: BOOT_INFO now says PC_SP. The
// watchdog's reset has put the peripherals and the clocks back to power-on, and crt0 has not run again,
// so the clocks are set up again (clk_peri, which times the UART, is off until they are) and the UART's
// blocks come out of reset here.
static void rebooted(void) {
    unreset_block_wait(RESETS_RESET_PLL_SYS_BITS | RESETS_RESET_PLL_USB_BITS);
    runtime_init_clocks();
    unreset_block_wait(RESETS_RESET_IO_BANK0_BITS | RESETS_RESET_PADS_BANK0_BITS | RESETS_RESET_UART0_BITS |
                       RESETS_RESET_TIMER0_BITS);
    stdio_init_all();
    printf("rebooted: reason 0x%lx, sp in the stack given %d\n", (unsigned long)watchdog_hw->reason,
           (uint32_t)__builtin_frame_address(0) > (uint32_t)stack && (uint32_t)__builtin_frame_address(0) <= (uint32_t)(stack + 256));
    print_boot_info("after the reboot");
    printf("done\n");
    __breakpoint();
}

int main(void) {
    stdio_init_all();
    const uint8_t *rom = (const uint8_t *)0;
    printf("rom: magic %c%c version %d.%d, git revision 0x%08lx\n", rom[0x10], rom[0x11], rom[0x12], rom[0x13],
           (unsigned long)*(const uint32_t *)rom_data_lookup(ROM_DATA_SOFTWARE_GIT_REVISION));

    const uint32_t codes[] = {ROM_FUNC_CONNECT_INTERNAL_FLASH, ROM_FUNC_FLASH_EXIT_XIP, ROM_FUNC_FLASH_RANGE_ERASE,
                              ROM_FUNC_FLASH_RANGE_PROGRAM, ROM_FUNC_FLASH_FLUSH_CACHE, ROM_FUNC_GET_SYS_INFO,
                              ROM_FUNC_REBOOT, ROM_FUNC_BOOTROM_STATE_RESET, ROM_FUNC_FLASH_SELECT_XIP_READ_MODE,
                              ROM_FUNC_OTP_ACCESS};
    int found = 0;
    for (unsigned i = 0; i < count_of(codes); i++) if (rom_func_lookup(codes[i])) found++;
    printf("lookup: %d of %d found, 'ZZ' %s\n", found, (int)count_of(codes),
           rom_func_lookup(ROM_TABLE_CODE('Z', 'Z')) ? "found" : "not found");

    print_boot_info("power-on");

    for (int i = 0; i < FLASH_PAGE_SIZE; i++) page[i] = (uint8_t)(i * 7 + 3);
    flash_range_erase(FLASH_AT, FLASH_SECTOR_SIZE);
    const uint8_t *flash = (const uint8_t *)(XIP_BASE + FLASH_AT);
    int erased = 1;
    for (int i = 0; i < FLASH_SECTOR_SIZE; i++) if (flash[i] != 0xff) erased = 0;
    flash_range_program(FLASH_AT, page, FLASH_PAGE_SIZE);
    printf("flash: erased %s, programmed %s, next page %s\n", erased ? "ok" : "BAD",
           memcmp(flash, page, FLASH_PAGE_SIZE) == 0 ? "ok" : "BAD", flash[FLASH_PAGE_SIZE] == 0xff ? "erased" : "BAD");
    uint8_t id[FLASH_UNIQUE_ID_SIZE_BYTES];
    flash_get_unique_id(id);
    printf("unique id:");
    for (int i = 0; i < FLASH_UNIQUE_ID_SIZE_BYTES; i++) printf(" %02x", id[i]);
    printf("\n");

    printf("rebooting to PC/SP\n");
    stdio_flush();
    int rc = rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_PC_SP, 10, (uint32_t)rebooted, (uint32_t)(stack + 256));
    printf("reboot returned %d\n", rc);
    for (;;) tight_loop_contents();
}
