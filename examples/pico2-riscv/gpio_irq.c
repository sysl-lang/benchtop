// GPIO interrupts on a Hazard3 hart, written against the registers alone (no SDK): pico-examples'
// hello_gpio_irq on RISC-V. GPIO 2 is watched for both edges through hart 0's PROC0 enables and
// IO_IRQ_BANK0 (21) in Hazard3's external interrupt controller; first GPIO 5, an SIO output reading its
// own pad, rises and falls; then four edges driven onto GPIO 2 from outside are reported as they come,
// each acknowledged in INTR by the handler, and the run ends at EBREAK. Addresses are the RP2350
// datasheet's (and the SDK's `hardware/regs`).

typedef unsigned int u32;
#define REG(a) (*(volatile u32 *)(a))
#define CLR 0x3000

#define RESETS 0x40020000u
#define RESETS_DONE (RESETS + 0x8)
#define CLK_PERI_CTRL (0x40010000u + 0x48)
#define IO_BANK0 0x40028000u
#define INTR0 (IO_BANK0 + 0x230)
#define PROC0_INTE0 (IO_BANK0 + 0x248)
#define PROC0_INTS0 (IO_BANK0 + 0x278)
#define PADS_BANK0 0x40038000u
#define UART0 0x40070000u
#define SIO 0xd0000000u

#define BIT_IO_BANK0 (1u << 6)
#define BIT_PADS_BANK0 (1u << 9)
#define BIT_UART0 (1u << 26)

#define IO_IRQ_BANK0 21
#define WATCH_PIN 2
#define LOOP_PIN 5

static volatile u32 events[16];
static volatile u32 count;

static void release(u32 bits) {
    REG(RESETS + CLR) = bits;
    while ((REG(RESETS_DONE) & bits) != bits) {}
}

// A GPIO given to function `f`: its pad out of isolation with its input enabled.
static void gpio_function(u32 n, u32 f) {
    REG(PADS_BANK0 + 4 + 4 * n) = (REG(PADS_BANK0 + 4 + 4 * n) & ~0x180u) | 0x40u;
    REG(IO_BANK0 + 8 * n + 4) = f;
}

static void puts_uart(const char *s) {
    for (; *s; s++) {
        while (REG(UART0 + 0x18) & 0x20) {}
        REG(UART0) = (u32)*s;
    }
}

// Every GPIO with an enabled event in INTS word 0: each acknowledged in INTR, and recorded as GPIO
// number and event mask.
__attribute__((interrupt("machine"), aligned(4))) static void isr(void) {
    u32 ints = REG(PROC0_INTS0);
    for (u32 g = 0; g < 8; g++) {
        u32 e = (ints >> (4 * g)) & 0xf;
        if (e) {
            REG(INTR0) = e << (4 * g);
            events[count++ & 15] = (g << 4) | e;
        }
    }
}

static const char *names[4] = {"LEVEL_LOW", "LEVEL_HIGH", "EDGE_FALL", "EDGE_RISE"};

static void report(u32 from) {
    for (u32 i = from; i < count; i++) {
        u32 e = events[i & 15];
        char line[] = "GPIO 0 ";
        line[5] = (char)('0' + (e >> 4));
        puts_uart(line);
        int first = 1;
        for (u32 k = 0; k < 4; k++) {
            if (e & (1u << k)) {
                if (!first) puts_uart(", ");
                puts_uart(names[k]);
                first = 0;
            }
        }
        puts_uart("\n");
    }
}

static void wait_for(u32 n) {
    while (count < n) __asm__ volatile("wfi");
}

void main(void) {
    release(BIT_IO_BANK0 | BIT_PADS_BANK0 | BIT_UART0);
    REG(CLK_PERI_CTRL) = (1u << 11);            // clk_peri on, from clk_sys
    gpio_function(0, 2);
    REG(UART0 + 0x24) = 6;                      // about 115200 baud from the ROSC's 11 MHz
    REG(UART0 + 0x28) = 0;
    REG(UART0 + 0x2c) = 0x70;                   // 8N1, FIFOs on
    REG(UART0 + 0x30) = 0x301;                  // UARTEN, TXE, RXE
    puts_uart("Hello GPIO IRQ\n");

    gpio_function(WATCH_PIN, 5);
    gpio_function(LOOP_PIN, 5);
    REG(SIO + 0x38) = 1u << LOOP_PIN;           // GPIO_OE_SET
    REG(INTR0) = 0xffffffffu;                   // nothing stale
    REG(PROC0_INTE0) = (0xcu << (4 * WATCH_PIN)) | (0xcu << (4 * LOOP_PIN));
    __asm__ volatile("csrw mtvec, %0" :: "r"(isr));
    __asm__ volatile("csrs 0xbe0, %0" :: "r"((1u << (16 + IO_IRQ_BANK0 - 16)) | 1u));  // meiea
    __asm__ volatile("csrs mie, %0" :: "r"(0x800));                                    // MEIE
    __asm__ volatile("csrs mstatus, %0" :: "r"(0x8));                                  // MIE

    REG(SIO + 0x18) = 1u << LOOP_PIN;           // GPIO_OUT_SET
    wait_for(1);
    REG(SIO + 0x20) = 1u << LOOP_PIN;           // GPIO_OUT_CLR
    wait_for(2);
    report(0);
    puts_uart("watching GPIO 2\n");
    for (u32 n = 3; n <= 6; n++) {
        wait_for(n);
        report(n - 1);
    }
    puts_uart("done\n");
}
