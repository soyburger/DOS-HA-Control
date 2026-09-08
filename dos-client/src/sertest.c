/* SERTEST -- standalone serial transmit test, no protocol/bridge involved.
 * Sends the byte 'U' (0x55, alternating 01010101 bit pattern) out the
 * configured COM port once a second, forever, using the exact same
 * INT14h calls as net_serial.c. Press any key to quit.
 *
 * Point of this file: isolate whether raw bytes can get out of DOS at
 * all on real hardware, independent of HACLIENT's higher-level protocol.
 * Watch with `cat /dev/ttyUSB0 | od -c` on the bridge side -- you should
 * see a steady stream of "U U U U ..." if the wire is carrying signal.
 */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
    int port = 0; /* COM1 */
    union REGS r;
    unsigned long count = 0;
    unsigned int _far *bda_com = (unsigned int _far *) _MK_FP(0x0040, 0x0000);
    unsigned int io_base;

    if (argc > 1)
        port = argv[1][0] - '1'; /* "1" -> COM1(0), "2" -> COM2(1), ... */

    io_base = bda_com[port];
    printf("SERTEST: COM%d io_base=0x%04X\n", port + 1, io_base);
    printf("SERTEST: sending 'U' every ~1s. Press any key to quit.\n");

    r.h.ah = 0x00;
    r.h.al = 0xE3; /* 9600 baud, no parity, 1 stop bit, 8 data bits */
    r.x.dx = port;
    int86(0x14, &r, &r);

    /* Explicitly assert DTR/RTS -- BIOS init above doesn't reliably do
     * this on every BIOS. See net_serial.c for the full explanation. */
    if (io_base)
        outp(io_base + 4, 0x03);

    for (;;) {
        r.h.ah = 0x01;
        r.h.al = 'U';
        r.x.dx = port;
        int86(0x14, &r, &r);

        count++;
        printf("sent #%lu, last AH=0x%02X\r", count, r.h.ah);

        if (kbhit()) {
            getch();
            break;
        }

        delay(1000);
    }

    printf("\nDone.\n");
    return 0;
}
