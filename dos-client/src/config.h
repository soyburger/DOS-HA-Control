#ifndef CONFIG_H
#define CONFIG_H

typedef enum { TRANSPORT_TCP, TRANSPORT_SERIAL } TransportKind;

typedef struct {
    TransportKind transport;

    /* TCP */
    char host[16];
    unsigned short port;

    /* Serial */
    int com_port;     /* 0=COM1 .. 3=COM4 */
    int baud_code;     /* SERIAL_BAUD_* from net_serial.h */

    int timeout_ticks; /* shared timeout for either transport */
} Config;

/* Reads a simple KEY=VALUE file (see HACONFIG.INI in this directory for the
 * format). Returns 0 on success, -1 if the file couldn't be opened. Missing
 * keys keep sensible defaults (TCP, 6321, COM1 9600, 5s timeout). */
int config_load(const char *path, Config *cfg);

#endif
