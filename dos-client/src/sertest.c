/* SERTEST -- standalone serial transmit test, no protocol/bridge involved.
 * Sends the byte 'U' (0x55, alternating 01010101 bit pattern) out the
 * configured COM port once a second, forever, using the exact same direct
 * UART register programming as net_serial.c (no BIOS INT 14h send/receive
 * calls). Press any key to quit.
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

#define UART_THR 0
#define UART_DLL 0
#define UART_DLM 1
#define UART_IER 1
#define UART_LCR 3
#define UART_MCR 4
#define UART_LSR 5

int main(int argc, char *argv[])
{
    int port = 0; /* COM1 */
    unsigned long count = 0;
    unsigned int _far *bda_com = (unsigned int _far *) _MK_FP(0x0040, 0x0000);
    unsigned int io_base;

    if (argc > 1)
        port = argv[1][0] - '1'; /* "1" -> COM1(0), "2" -> COM2(1), ... */

    io_base = bda_com[port];
    printf("SERTEST: COM%d io_base=0x%04X\n", port + 1, io_base);
    if (io_base == 0) {
        printf("BIOS reports no port present at COM%d -- aborting.\n", port + 1);
        return 1;
    }
    printf("SERTEST: sending 'U' every ~1s via direct UART I/O. "
           "Press any key to quit.\n");

    outp(io_base + UART_IER, 0x00);
    outp(io_base + UART_LCR, 0x80);       /* DLAB=1 */
    outp(io_base + UART_DLL, 12);         /* 115200/12 = 9600 baud */
    outp(io_base + UART_DLM, 0);
    outp(io_base + UART_LCR, 0x03);       /* DLAB=0, 8N1 */
    outp(io_base + UART_MCR, 0x03);       /* DTR + RTS up */

    for (;;) {
        unsigned char lsr;

        while (!((lsr = (unsigned char) inp(io_base + UART_LSR)) & 0x20))
            ; /* wait for transmitter holding register empty */
        outp(io_base + UART_THR, 'U');

        count++;
        printf("sent #%lu, LSR=0x%02X\r", count, lsr);

        if (kbhit()) {
            getch();
            break;
        }

        delay(1000);
    }

    printf("\nDone.\n");
    return 0;
}
