/* Serial transport via BIOS INT 14h. No packet driver, no mTCP -- this is
 * the simplest possible link: DOS <-> bridge over an RS-232 cable at a
 * fixed baud rate, 8N1. INT 14h caps out at 9600 baud on most BIOSes,
 * which is plenty for a line-oriented text protocol.
 */
#include <dos.h>
#include <string.h>
#include "net_serial.h"

static int s_port = -1;
static int s_timeout_ticks = 90; /* ~5 sec at 18.2 ticks/sec */

/* BIOS tick count at 0040:006C, incremented ~18.2 times/sec. */
static unsigned long bios_ticks(void)
{
    union REGS r;
    r.h.ah = 0x00;
    int86(0x1A, &r, &r);
    return ((unsigned long) r.x.cx << 16) | r.x.dx;
}

int serial_open(int port, int baud_code, int timeout_ticks)
{
    union REGS r;

    s_port = port;
    s_timeout_ticks = timeout_ticks;

    /* AH=00h Initialize port. AL: bits7-5 baud, 4-3 parity(00=none),
     * bit2 stop bits(0=1), bits1-0 word length(11b=8 bits). */
    r.h.ah = 0x00;
    r.h.al = (unsigned char) ((baud_code << 5) | 0x03);
    r.x.dx = port;
    int86(0x14, &r, &r);

    /* AH bit7 of the returned line status = timeout/error on some BIOSes
     * during init; the more reliable check is a status call. */
    r.h.ah = 0x03;
    r.x.dx = port;
    int86(0x14, &r, &r);

    return 0; /* INT 14h has no reliable "port absent" signal; caller should
                 confirm the link with proto_ping() after opening. */
}

static int serial_putc(char c)
{
    union REGS r;
    r.h.ah = 0x01;
    r.h.al = (unsigned char) c;
    r.x.dx = s_port;
    int86(0x14, &r, &r);
    /* bit7 of AH set on write timeout/error */
    return (r.h.ah & 0x80) ? -1 : 0;
}

static int serial_write_line(const char *line)
{
    while (*line) {
        if (serial_putc(*line) < 0)
            return -1;
        line++;
    }
    if (serial_putc('\r') < 0) return -1;
    if (serial_putc('\n') < 0) return -1;
    return 0;
}

/* Non-blocking-ish read of one char: returns the char (0-255), or -1 if
 * none is waiting right now (caller should poll/timeout around this). */
static int serial_poll_char(void)
{
    union REGS r;

    r.h.ah = 0x03; /* status */
    r.x.dx = s_port;
    int86(0x14, &r, &r);

    if (!(r.h.ah & 0x01)) /* bit0 = data ready */
        return -1;

    r.h.ah = 0x02; /* receive char (returns immediately, data is ready) */
    r.x.dx = s_port;
    int86(0x14, &r, &r);

    if (r.h.ah & 0x80) /* timeout/error bit */
        return -1;

    return r.h.al;
}

static int serial_read_line(char *buf, int buflen)
{
    int n = 0;
    unsigned long start = bios_ticks();

    for (;;) {
        int c = serial_poll_char();

        if (c < 0) {
            if (bios_ticks() - start > (unsigned long) s_timeout_ticks)
                return -1; /* timed out waiting for a line */
            continue;
        }

        start = bios_ticks(); /* reset timeout once data is flowing */

        if (c == '\n')
            break;
        if (c == '\r')
            continue;

        if (n < buflen - 1)
            buf[n++] = (char) c;
    }

    buf[n] = '\0';
    return n;
}

void serial_get_transport(Transport *t)
{
    t->read_line = serial_read_line;
    t->write_line = serial_write_line;
}
