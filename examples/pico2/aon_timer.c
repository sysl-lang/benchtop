// The always-on timer through pico_aon_timer and hardware_powman: started from the crystal at a
// wall-clock time, read after a TIMER0 sleep, an alarm waited for with WFI, an alarm already past,
// a stop that holds the count, and the carry between READ_TIME_LOWER and READ_TIME_UPPER.
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/aon_timer.h"
#include "hardware/powman.h"
#include "hardware/structs/powman.h"
#include "hardware/sync.h"

static volatile int fired;

static void on_alarm(void) {
    fired++;
}

static void print_time(const char *what) {
    struct timespec ts;
    aon_timer_get_time(&ts);
    printf("%s %lu.%03lu\n", what, (unsigned long)ts.tv_sec, (unsigned long)(ts.tv_nsec / 1000000));
}

int main(void) {
    stdio_init_all();

    struct timespec start = {.tv_sec = 1700000000, .tv_nsec = 0};
    aon_timer_start(&start);
    printf("running %d using_xosc %d\n", aon_timer_is_running(), (powman_hw->timer & POWMAN_TIMER_USING_XOSC_BITS) != 0);

    sleep_ms(250);
    print_time("after 250 ms:");

    // An alarm 1.5 s ahead, waited for asleep.
    struct timespec at;
    aon_timer_get_time(&at);
    at.tv_sec += 1;
    at.tv_nsec += 500000000;
    if (at.tv_nsec >= 1000000000) {
        at.tv_nsec -= 1000000000;
        at.tv_sec++;
    }
    aon_timer_enable_alarm(&at, on_alarm, false);
    while (!fired) __wfi();
    print_time("alarm fired at");
    printf("alarm latched %d irq pending %d\n", (powman_hw->timer & POWMAN_TIMER_ALARM_BITS) != 0,
           (powman_hw->ints & POWMAN_INTS_TIMER_BITS) != 0);

    // Cleared while still enabled and past, it latches again; disabled first, it clears.
    powman_clear_alarm();
    printf("cleared while enabled: alarm %d\n", (powman_hw->timer & POWMAN_TIMER_ALARM_BITS) != 0);
    aon_timer_disable_alarm();
    powman_clear_alarm();
    printf("disabled and cleared: alarm %d\n", (powman_hw->timer & POWMAN_TIMER_ALARM_BITS) != 0);

    // An alarm already past fires at once.
    struct timespec past = {.tv_sec = 1700000000, .tv_nsec = 0};
    aon_timer_enable_alarm(&past, on_alarm, false);
    busy_wait_us(10);
    printf("past alarm fired %d\n", fired);
    aon_timer_disable_alarm();
    powman_clear_alarm();

    // Stopped, the count holds.
    aon_timer_stop();
    uint64_t held = powman_timer_get_ms();
    sleep_ms(100);
    printf("stopped: running %d held %d\n", aon_timer_is_running(), powman_timer_get_ms() == held);

    // The carry from the low word into the high one.
    powman_timer_set_ms(0xFFFFFFF0ull);
    powman_timer_start();
    uint64_t t0 = time_us_64();
    sleep_ms(25);
    uint64_t ms = powman_timer_get_ms();
    uint64_t t1 = time_us_64();
    printf("carry: upper %lu lower %lu after %lu us\n", (unsigned long)(ms >> 32), (unsigned long)(ms & 0xFFFFFFFFu),
           (unsigned long)(t1 - t0));
    printf("done\n");
    __breakpoint();
}
