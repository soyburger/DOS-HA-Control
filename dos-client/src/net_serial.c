/* Serial transport via direct 8250/16450/16550 UART register programming.
 * No packet driver, no mTCP -- this is the simplest possible link: DOS <->
 * bridge over an RS-232 cable at a fixed baud rate, 8N1.
 *
 * This deliberately bypasses BIOS INT 14h for the actual send/receive/init
 * work (only the BIOS Data Area's port-presence table is still used, to
 * find the I/O base address and confirm a port exists at all). INT 14h
 * implementations vary in quality across BIOS vendors and were historically
 * considered unreliable for real data transfer by serious DOS comms
 * software (Telix, Procomm, Kermit), which is exactly why those programs
 * all programmed the UART directly instead. The 8250-family register
 * layout used below is a hardware standard, unchanged since the original
 * IBM PC and identical across every BIOS vendor and DOS version.
 */
#include <dos.h>
#include <i86.h>
#include <conio.h>
#include <string.h>
#include "net_serial.h"

/* 8250/16450/16550 register offsets from the UART's I/O base. */
#define UART_THR 0 /* Transmitter Holding Register (write, DLAB=0) */
#define UART_RBR 0 /* Receiver Buffer Register (read, DLAB=0) */
#define UART_DLL 0 /* Divisor Latch LSB (DLAB=1) */
#define UART_DLM 1 /* Divisor Latch MSB (DLAB=1) */
#define UART_IER 1 /* Interrupt Enable Register (DLAB=0) */
#define UART_LCR 3 /* Line Control Register */
#define UART_MCR 4 /* Modem Control Register */
#define UART_LSR 5 /* Line Status Register */

#define LCR_8N1       0x03 /* 8 data bits, no parity, 1 stop bit */
#define LCR_DLAB      0x80
#define MCR_DTR_RTS   0x03
#define LSR_DATA_READY 0x01
#define LSR_THRE       0x20 /* transmitter holding register empty */

static unsigned int s_io_base = 0;
static int s_timeout_ticks = 90; /* ~5 sec at 18.2 ticks/sec */

/* BIOS tick count at 0040:006C, incremented ~18.2 times/sec. */
static unsigned long bios_ticks(void)
{
    union REGS r;
    r.h.ah = 0x00;
    int86(0x1A, &r, &r);
    return ((unsigned long) r.x.cx << 16) | r.x.dx;
}

static unsigned int baud_divisor(int baud_code)
{
    /* Divisor = 115200 / desired baud. */
    switch (baud_code) {
        case SERIAL_BAUD_1200: return 96;
        case SERIAL_BAUD_2400: return 48;
        case SERIAL_BAUD_4800: return 24;
        case SERIAL_BAUD_9600:
        default:               return 12;
    }
}

int serial_open(int port, int baud_code, int timeout_ticks)
{
    /* The BIOS records each detected COM port's I/O base address in the
     * BIOS Data Area at 0040:0000 (COM1) through 0040:0006 (COM4), one
     * 16-bit word per port, 0x0000 meaning "not present at boot." This is
     * the same table DOS's own MODE command checks before trusting a COM
     * port exists -- we still use it just to find the address and confirm
     * presence, nothing else from here on goes through the BIOS. */
    unsigned int _far *bda_com = (unsigned int _far *) _MK_FP(0x0040, 0x0000);
    unsigned int divisor;

    if (port < 0 || port > 3 || bda_com[port] == 0)
        return -1;

    s_io_base = bda_com[port];
    s_timeout_ticks = timeout_ticks;
    divisor = baud_divisor(baud_code);

    outp(s_io_base + UART_IER, 0x00);      /* disable UART interrupts -- we poll */
    outp(s_io_base + UART_LCR, LCR_DLAB);  /* expose the divisor latch */
    outp(s_io_base + UART_DLL, (unsigned char) (divisor & 0xFF));
    outp(s_io_base + UART_DLM, (unsigned char) ((divisor >> 8) & 0xFF));
    outp(s_io_base + UART_LCR, LCR_8N1);   /* DLAB=0, 8N1 */
    outp(s_io_base + UART_MCR, MCR_DTR_RTS); /* DTR + RTS up */

    return 0;
}

static int serial_putc(char c)
{
    unsigned long start = bios_ticks();

    while (!(inp(s_io_base + UART_LSR) & LSR_THRE)) {
        if (bios_ticks() - start > 18) /* ~1s hard cap so a wedged UART can't hang forever */
            return -1;
    }
    outp(s_io_base + UART_THR, (unsigned char) c);
    return 0;
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

/* Non-blocking read of one char: returns the char (0-255), or -1 if none
 * is waiting right now (caller should poll/timeout around this). */
static int serial_poll_char(void)
{
    if (!(inp(s_io_base + UART_LSR) & LSR_DATA_READY))
        return -1;
    return inp(s_io_base + UART_RBR);
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
