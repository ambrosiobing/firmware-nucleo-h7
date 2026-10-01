/* shared/board/retarget.c: printf to the console, and the cost of that decision.
 *
 * The newlib hook is _write. Implementing it is all that printf needs, and it is
 * the only hook this volume implements: nothing here opens a file, allocates, or
 * asks the time of the C library.
 *
 * TWO THINGS WORTH KNOWING BEFORE USING printf ON THIS PART.
 *
 * Which C library. The build links nano.specs, the reduced newlib, because the
 * full one costs tens of kilobytes of flash for formatting this volume never
 * uses. The reduced one omits floating point formatting from printf by default,
 * which is the usual surprise: a %f prints nothing useful. Every project here
 * prints integers for that reason, and where a real number is wanted it is
 * printed as a scaled integer with its scale named.
 *
 * Reentrancy. printf is not reentrant in either library, so calling it from an
 * interrupt handler and from the main loop corrupts its internal state. Nothing
 * in this volume prints from a handler. P03 is where that constraint becomes the
 * subject rather than a footnote, because a handler that wants to report
 * something has to hand it to the main loop through the ring buffer of P02.
 *
 * The cheaper alternative is named and not used: the debug probe can carry a
 * trace channel that costs no peripheral and far fewer cycles, but the fastest
 * implementation of it is licensed for use only with one vendor's probe, so it
 * is named here and avoided.
 */
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>

#include "board.h"

/* newlib's own signatures, which are not the POSIX ones. _write takes a
 * non-const char pointer and an int length, and getting that wrong does not
 * merely warn: the symbol does not match the one the library calls, the library
 * keeps its own stub, and printf silently produces nothing. Caught on
 * Thursday 1 October 2026 by syntax-checking against a host header, which
 * objected for its own reasons and was right about the shape. */
#define STDOUT_FD 1
#define STDERR_FD 2

/* THIS FILE IS THE ONE THAT CANNOT BE SYNTAX-CHECKED ON THE HOST. The mingw
 * headers declare _write themselves, with their own signature, so a host check
 * objects no matter which form is used here. Every other file in shared/board
 * does pass a host syntax check, and that check is worth running on them for
 * exactly the reason it was worth running here: it found that the signature was
 * wrong in the first place. This file waits for a cross compiler. */

/* The one hook printf needs. Characters go to the console; a line feed is
 * expanded to carriage return and line feed, because a terminal on the host
 * expects both and a log that reads as one long line is a small daily cost. */
int _write(int fd, char *buf, int len)
{
    if (fd != STDOUT_FD && fd != STDERR_FD) {
        errno = EBADF;
        return -1;
    }
    if (buf == NULL || len < 0) {
        errno = EINVAL;
        return -1;
    }

    /* No console means the bytes are discarded, and saying so through the return
     * value is better than pretending. A caller that ignores the return of
     * printf, which is nearly every caller, then simply sees nothing, which is
     * the truth: there is no console. board_console_status() is how a project
     * finds that out deliberately. */
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            if (!board_console_put('\r')) {
                return i;
            }
        }
        if (!board_console_put(buf[i])) {
            return i;
        }
    }
    return len;
}

/* The rest of the hooks newlib wants at link time. Each is the smallest honest
 * answer rather than a stub that pretends to succeed, so a call that should
 * never happen fails loudly instead of returning a plausible value. */

int _read(int fd, char *buf, int len)
{
    (void) fd; (void) buf; (void) len;
    errno = ENOSYS;              /* P03 adds receive; P01 does not */
    return -1;
}

int _close(int fd)              { (void) fd; errno = ENOSYS; return -1; }
int _lseek(int fd, int o, int w) { (void) fd; (void) o; (void) w; errno = ENOSYS; return -1; }

int _isatty(int fd)
{
    /* The console is a terminal, which stops the library buffering output it
     * would otherwise hold until a flush that never comes. */
    return (fd == STDOUT_FD || fd == STDERR_FD) ? 1 : 0;
}

int _fstat(int fd, struct stat *st)
{
    if (st == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (fd == STDOUT_FD || fd == STDERR_FD) {
        st->st_mode = S_IFCHR;
        return 0;
    }
    errno = EBADF;
    return -1;
}

/* No heap is used by anything in this volume, and this says so rather than
 * quietly handing out memory. The linker script reserves a kilobyte only so that
 * a library call which assumes a heap exists does not fail at link time; if this
 * is ever reached, something started allocating and that is worth discovering
 * here rather than in a fragmentation problem months later. */
void *_sbrk(ptrdiff_t increment)
{
    (void) increment;
    errno = ENOMEM;
    return (void *) -1;
}
