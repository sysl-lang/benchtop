// A Pico 2 booted on its Hazard3 cores, written against the registers alone (no SDK): bring up the
// crystal, PLL_SYS at 150 MHz and the 1 µs ticks; print over UART0 on GP0; launch core 1 through the
// boot ROM's FIFO protocol, which drives GP15 high; then blink the LED on GP25 three times, sleeping in
// WFI on the RISC-V machine timer between edges, and end at EBREAK. Addresses are the RP2350
// datasheet's (and the SDK's `hardware/regs`).

typedef unsigned int u32;
#define REG(a) (*(volatile u32 *)(a))
#define CLR 0x3000

#define RESETS 0x40020000u
#define RESETS_DONE (RESETS + 0x8)
#define CLOCKS 0x40010000u
#define CLK_REF_CTRL (CLOCKS + 0x30)
#define CLK_REF_SELECTED (CLOCKS + 0x38)
#define CLK_SYS_CTRL (CLOCKS + 0x3c)
#define CLK_SYS_SELECTED (CLOCKS + 0x44)
#define CLK_PERI_CTRL (CLOCKS + 0x48)
#define XOSC 0x40048000u
#define PLL_SYS 0x40050000u
#define IO_BANK0 0x40028000u
#define PADS_BANK0 0x40038000u
#define UART0 0x40070000u
#define TICKS 0x40108000u
#define SIO 0xd0000000u

#define BIT_IO_BANK0 (1u << 6)
#define BIT_PADS_BANK0 (1u << 9)
#define BIT_PLL_SYS (1u << 14)
#define BIT_TIMER0 (1u << 23)
#define BIT_UART0 (1u << 26)

#define LED 25
#define CORE1_PIN 15

extern char __core1_stack_top[];
extern void trap(void);

static void release(u32 bits) {
    REG(RESETS + CLR) = bits;
    while ((REG(RESETS_DONE) & bits) != bits) {}
}

// A GPIO given to function `f`: its pad out of isolation with its input enabled.
static void gpio_function(u32 n, u32 f) {
    REG(PADS_BANK0 + 4 + 4 * n) = (REG(PADS_BANK0 + 4 + 4 * n) & ~0x180u) | 0x40u;
    REG(IO_BANK0 + 8 * n + 4) = f;
}

static void clocks_init(void) {
    REG(XOSC + 0xc) = 47;                       // STARTUP.DELAY: about 1 ms of the 12 MHz crystal
    REG(XOSC + 0x0) = (0xfabu << 12) | 0xaa0;   // CTRL: ENABLE, 1-15 MHz
    while ((REG(XOSC + 0x4) & 0x80000000u) == 0) {}
    REG(CLK_REF_CTRL) = 2;                      // clk_ref from the crystal
    while (REG(CLK_REF_SELECTED) != 4) {}
    release(BIT_PLL_SYS);
    REG(PLL_SYS + 0x8) = 125;                   // FBDIV: 1500 MHz VCO
    REG(PLL_SYS + 0x4 + CLR) = 0x21;            // power the PLL and its VCO
    while ((REG(PLL_SYS + 0x0) & 0x80000000u) == 0) {}
    REG(PLL_SYS + 0xc) = (5u << 16) | (2u << 12);   // post dividers 5 and 2: 150 MHz
    REG(PLL_SYS + 0x4 + CLR) = 0x8;
    REG(CLK_SYS_CTRL) = 1;                      // clk_sys from its aux mux, PLL_SYS
    while (REG(CLK_SYS_SELECTED) != 2) {}
    REG(CLK_PERI_CTRL) = (1u << 11);            // clk_peri on, from clk_sys
    REG(TICKS + 0x1c) = 12;                     // TIMER0's tick: 12 cycles of clk_ref
    REG(TICKS + 0x18) = 1;
    REG(TICKS + 0x40) = 12;                     // the RISC-V platform timer's tick
    REG(TICKS + 0x3c) = 1;
}

static void uart_init(void) {
    release(BIT_UART0);
    gpio_function(0, 2);
    REG(UART0 + 0x24) = 81;                     // 115200 baud from 150 MHz
    REG(UART0 + 0x28) = 24;
    REG(UART0 + 0x2c) = 0x70;                   // 8N1, FIFOs on
    REG(UART0 + 0x30) = 0x301;                  // UARTEN, TXE, RXE
}

static void puts_uart(const char *s) {
    for (; *s; s++) {
        while (REG(UART0 + 0x18) & 0x20) {}
        REG(UART0) = (u32)*s;
    }
}

// Sleep until the machine timer reaches `us` microseconds from now.
static void sleep_us(u32 us) {
    u32 now = REG(SIO + 0x1b0);
    REG(SIO + 0x1bc) = 0xffffffffu;
    REG(SIO + 0x1b8) = now + us;
    REG(SIO + 0x1bc) = REG(SIO + 0x1b4) + (now + us < now);
    __asm__ volatile("csrs mie, %0" :: "r"(0x80));
    while (REG(SIO + 0x1b4) < REG(SIO + 0x1bc) || REG(SIO + 0x1b0) < REG(SIO + 0x1b8))
        __asm__ volatile("wfi");
    __asm__ volatile("csrc mie, %0" :: "r"(0x80));
}

static void fifo_push(u32 x) {
    while ((REG(SIO + 0x50) & 2) == 0) {}
    REG(SIO + 0x54) = x;
}

static u32 fifo_pop(void) {
    while ((REG(SIO + 0x50) & 1) == 0) {}
    return REG(SIO + 0x58);
}

static void core1_main(void) {
    REG(SIO + 0x38) = 1u << CORE1_PIN;          // GPIO_OE_SET
    REG(SIO + 0x18) = 1u << CORE1_PIN;          // GPIO_OUT_SET
    for (;;) __asm__ volatile("wfi");
}

// The boot ROM's launch sequence, each word echoed, restarting on a wrong echo.
static void launch_core1(void) {
    const u32 seq[6] = {0, 0, 1, (u32)trap, (u32)__core1_stack_top, (u32)core1_main};
    u32 i = 0;
    while (i < 6) {
        if (seq[i] == 0) {
            while (REG(SIO + 0x50) & 1) (void)REG(SIO + 0x58);
        }
        fifo_push(seq[i]);
        i = fifo_pop() == seq[i] ? i + 1 : 0;
    }
}

void main(void) {
    release(BIT_IO_BANK0 | BIT_PADS_BANK0 | BIT_TIMER0);
    clocks_init();
    uart_init();
    puts_uart("hello from hazard3\n");
    gpio_function(CORE1_PIN, 5);
    launch_core1();
    puts_uart("core 1 launched\n");
    gpio_function(LED, 5);
    REG(SIO + 0x38) = 1u << LED;
    for (int i = 0; i < 6; i++) {
        REG(SIO + 0x28) = 1u << LED;            // GPIO_OUT_XOR
        puts_uart((i & 1) ? "led off\n" : "led on\n");
        sleep_us(10000);
    }
    puts_uart("done\n");
}
