// The pseudo-terminal behind `benchtop run --usb pty` (pty.sysl): its controlling side opened
// non-blocking, and its terminal side set raw, so the bytes the guest's CDC port sends reach whatever
// opens the terminal unchanged, and nothing written there is echoed back as input. `struct termios`
// and `fcntl`'s variadic third argument stay on this side.
#define _XOPEN_SOURCE 600
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

// Raw mode on `fd`, a terminal: no echo, no line editing, no signals, no translation either way.
static int benchtop_raw(int fd) {
    struct termios t;
    if (tcgetattr(fd, &t) != 0) return -errno;
    cfmakeraw(&t);
    if (tcsetattr(fd, TCSANOW, &t) != 0) return -errno;
    return 0;
}

static int benchtop_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) return -errno;
    return 0;
}

// A new pseudo-terminal: its controlling side's descriptor in fds[0], non-blocking, and in fds[1] a
// descriptor of its terminal side, which benchtop holds open so that the terminal has a reader from
// the start and does not hang up when a program that opened it closes it. The terminal's name goes
// into `name` (`room` bytes, NUL-terminated). 0, or a negative errno.
int benchtop_pty_open(int *fds, char *name, int room) {
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0) return -errno;
    int e = 0;
    const char *n = NULL;
    if (grantpt(m) != 0 || unlockpt(m) != 0 || (n = ptsname(m)) == NULL) e = -errno;
    if (e == 0 && (int)strlen(n) >= room) e = -ENAMETOOLONG;
    int s = -1;
    if (e == 0) {
        strcpy(name, n);
        s = open(name, O_RDWR | O_NOCTTY);
        if (s < 0) e = -errno;
    }
    if (e == 0) e = benchtop_raw(s);
    if (e == 0) e = benchtop_nonblocking(m);
    if (e != 0) {
        if (s >= 0) close(s);
        close(m);
        return e;
    }
    fds[0] = m;
    fds[1] = s;
    return 0;
}

// The terminal side of the pseudo-terminal `name`, opened as a serial program opens a port: raw and
// non-blocking. The descriptor, or a negative errno.
int benchtop_pty_attach(const char *name) {
    int fd = open(name, O_RDWR | O_NOCTTY);
    if (fd < 0) return -errno;
    int e = benchtop_raw(fd);
    if (e == 0) e = benchtop_nonblocking(fd);
    if (e != 0) {
        close(fd);
        return e;
    }
    return fd;
}

// Up to `room` bytes read from `fd` without waiting: the count, 0 where none are waiting, or a
// negative errno.
long benchtop_pty_read(int fd, uint8_t *into, long room) {
    ssize_t k = read(fd, into, (size_t)room);
    if (k >= 0) return (long)k;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    return -errno;
}

// Up to `len` bytes written to `fd` without waiting: how many were taken, 0 where it has no room now,
// or a negative errno.
long benchtop_pty_write(int fd, const uint8_t *from, long len) {
    ssize_t k = write(fd, from, (size_t)len);
    if (k >= 0) return (long)k;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    return -errno;
}

int benchtop_pty_close(int fd) {
    return close(fd) == 0 ? 0 : -errno;
}

// errno's text for the negative `code` one of these answered.
const char *benchtop_pty_error(int code) {
    return strerror(-code);
}
