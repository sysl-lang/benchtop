// The RP2350's OTP through Raspberry Pi's boot ROM, as the SDK reaches it: the chip ID through the
// ECC read window and through the ROM's `get_sys_info` (with the critical flags and the core's
// architecture, which it reads from the OTP block), a user row written with ECC through
// `otp_access` and read back three ways (the ROM, the ECC window and the raw window, which shows the
// codeword the ROM programmed), a raw row, a write that would clear a bit, each data bit's codeword as
// the ROM's encoder makes it, an ECC write over a row with a bit already set (which the ROM stores
// inverted, with both BRBP bits), a page locked by SW_LOCK refusing a write and then any access, and
// the ROM's argument checks.
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/structs/otp.h"
#include "hardware/regs/addressmap.h"

#define USER_ROW 0x400
#define RAW_ROW 0x402
#define LOCKED_ROW 0x440
#define HIDDEN_ROW 0x480
#define CODEWORD_ROWS 0x500
#define OVER_ROW 0x520

static const volatile uint16_t *const otp_ecc = (const volatile uint16_t *)OTP_DATA_BASE;
static const volatile uint32_t *const otp_raw = (const volatile uint32_t *)OTP_DATA_RAW_BASE;

static int access(void *buf, uint32_t len, uint32_t row, uint32_t flags) {
    otp_cmd_t cmd = {.flags = row | flags};
    return rom_func_otp_access((uint8_t *)buf, len, cmd);
}

int main(void) {
    stdio_init_all();

    printf("chip id: %04x %04x %04x %04x\n", otp_ecc[0], otp_ecc[1], otp_ecc[2], otp_ecc[3]);
    uint32_t info[4] = {0};
    int words = rom_get_sys_info(info, 4, SYS_INFO_CHIP_INFO);
    printf("get_sys_info: %d words, package 0x%lx, device 0x%08lx, wafer 0x%08lx\n", words,
           (unsigned long)info[1], (unsigned long)info[2], (unsigned long)info[3]);

    words = rom_get_sys_info(info, 3, SYS_INFO_CRITICAL | SYS_INFO_CPU_INFO);
    printf("get_sys_info: %d words, flags 0x%lx, critical 0x%lx, cpu %s\n", words, (unsigned long)info[0],
           (unsigned long)info[1], info[2] ? "RISC-V" : "Arm");

    uint16_t id[4] = {0};
    int rc = access(id, sizeof id, 0, OTP_CMD_ECC_BITS);
    printf("otp_access chip id: %d, %04x %04x %04x %04x\n", rc, id[0], id[1], id[2], id[3]);

    uint16_t blank[2] = {0xffff, 0xffff};
    rc = access(blank, sizeof blank, USER_ROW, OTP_CMD_ECC_BITS);
    printf("user rows before: %d, %04x %04x\n", rc, blank[0], blank[1]);

    uint16_t data[2] = {0x1234, 0xbeef};
    rc = access(data, sizeof data, USER_ROW, OTP_CMD_WRITE_BITS | OTP_CMD_ECC_BITS);
    uint16_t back[2] = {0};
    int rc2 = access(back, sizeof back, USER_ROW, OTP_CMD_ECC_BITS);
    printf("ecc write: %d, read back %d, %04x %04x\n", rc, rc2, back[0], back[1]);
    printf("ecc window: %04x %04x, raw window: %06lx %06lx\n", otp_ecc[USER_ROW], otp_ecc[USER_ROW + 1],
           (unsigned long)otp_raw[USER_ROW], (unsigned long)otp_raw[USER_ROW + 1]);

    uint32_t raw = 0x00a5a5c3;
    rc = access(&raw, sizeof raw, RAW_ROW, OTP_CMD_WRITE_BITS);
    uint32_t raw_back = 0;
    rc2 = access(&raw_back, sizeof raw_back, RAW_ROW, 0);
    printf("raw write: %d, read back %d, %06lx\n", rc, rc2, (unsigned long)raw_back);

    uint32_t clear = 0x00a5a5c2;
    rc = access(&clear, sizeof clear, RAW_ROW, OTP_CMD_WRITE_BITS);
    printf("clearing a bit: %d, row still %06lx\n", rc, (unsigned long)otp_raw[RAW_ROW]);

    printf("codewords:");
    for (int i = 0; i < 16; i++) {
        uint16_t bit = (uint16_t)(1u << i);
        access(&bit, sizeof bit, CODEWORD_ROWS + i, OTP_CMD_WRITE_BITS | OTP_CMD_ECC_BITS);
        printf(" %06lx", (unsigned long)otp_raw[CODEWORD_ROWS + i]);
    }
    printf("\n");

    uint32_t one = 1;
    access(&one, sizeof one, OVER_ROW, OTP_CMD_WRITE_BITS);
    uint16_t over = 0xfffe;
    rc = access(&over, sizeof over, OVER_ROW, OTP_CMD_WRITE_BITS | OTP_CMD_ECC_BITS);
    uint16_t over_back = 0;
    rc2 = access(&over_back, sizeof over_back, OVER_ROW, OTP_CMD_ECC_BITS);
    printf("ecc over a set bit: %d, raw %06lx, read back %d, %04x\n", rc, (unsigned long)otp_raw[OVER_ROW], rc2, over_back);

    otp_hw->sw_lock[LOCKED_ROW / 64] = OTP_SW_LOCK0_SEC_VALUE_READ_ONLY << OTP_SW_LOCK0_SEC_LSB;
    uint16_t more = 0x5555;
    rc = access(&more, sizeof more, LOCKED_ROW, OTP_CMD_WRITE_BITS | OTP_CMD_ECC_BITS);
    uint16_t locked_back = 0xffff;
    rc2 = access(&locked_back, sizeof locked_back, LOCKED_ROW, OTP_CMD_ECC_BITS);
    printf("read-only page: sw_lock 0x%lx, write %d, read %d, %04x\n", (unsigned long)otp_hw->sw_lock[LOCKED_ROW / 64],
           rc, rc2, locked_back);

    otp_hw->sw_lock[HIDDEN_ROW / 64] = OTP_SW_LOCK0_SEC_VALUE_INACCESSIBLE << OTP_SW_LOCK0_SEC_LSB;
    uint16_t hidden = 0;
    rc = access(&hidden, sizeof hidden, HIDDEN_ROW, OTP_CMD_ECC_BITS);
    rc2 = access(&more, sizeof more, HIDDEN_ROW, OTP_CMD_WRITE_BITS | OTP_CMD_ECC_BITS);
    printf("inaccessible page: sw_lock 0x%lx, read %d, write %d\n", (unsigned long)otp_hw->sw_lock[HIDDEN_ROW / 64], rc, rc2);

    otp_hw->sw_lock[HIDDEN_ROW / 64] = 0;
    printf("locks only advance: sw_lock 0x%lx\n", (unsigned long)otp_hw->sw_lock[HIDDEN_ROW / 64]);

    rc = access(back, sizeof back, 0x1000, OTP_CMD_ECC_BITS);
    rc2 = access(back, 3, USER_ROW, OTP_CMD_ECC_BITS);
    printf("past the last row: %d, odd length: %d\n", rc, rc2);
    printf("done\n");
    __breakpoint();
}
