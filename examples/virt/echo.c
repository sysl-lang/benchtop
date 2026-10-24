// Greets, then echoes every line typed back with a line count, until the input ends or Ctrl-D.

#include "virt.h"

static void put_number(unsigned n) {
    char digits[10];
    int k = 0;

    do {
        digits[k++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);

    while (k) put_char(digits[--k]);
}

int main(void) {
    unsigned lines = 0;

    put_string("hello from an M0+ on cortical's virtual board\n");
    put_string("type a line; Ctrl-D ends\n> ");

    for (;;) {
        int c = get_char();

        if (c < 0 || c == 4) break;
        if (c == '\r') c = '\n';

        put_char((char)c);

        if (c == '\n') {
            lines++;
            put_string("> ");
        }
    }

    put_string("\nbye after ");
    put_number(lines);
    put_string(" lines\n");
    return 0;
}
