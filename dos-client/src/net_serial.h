#ifndef NET_SERIAL_H
#define NET_SERIAL_H

#include "protocol.h"

#define SERIAL_COM1 0
#define SERIAL_COM2 1
#define SERIAL_COM3 2
#define SERIAL_COM4 3

/* baud_code: one of the SERIAL_BAUD_* constants below (matches BIOS INT 14h
 * encoding used by most 486-era BIOSes; INT 14h tops out at 9600). */
#define SERIAL_BAUD_1200   4
#define SERIAL_BAUD_2400   5
#define SERIAL_BAUD_4800   6
#define SERIAL_BAUD_9600   7

/* Opens the port via BIOS INT 14h (8 data bits, no parity, 1 stop bit).
 * Returns 0 on success, -1 if the BIOS reports no port present.
 * timeout_ticks: how long serial_read_line waits for a full line, in BIOS
 * clock ticks (~18.2/sec) -- e.g. 90 ticks =~ 5 seconds. */
int serial_open(int port, int baud_code, int timeout_ticks);

/* Populates a Transport that reads/writes over the opened port. */
void serial_get_transport(Transport *t);

#endif
