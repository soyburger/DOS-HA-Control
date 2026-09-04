#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    int com_port;      /* 0=COM1 .. 3=COM4 */
    int baud_code;      /* SERIAL_BAUD_* from net_serial.h */
    int timeout_ticks;  /* how long to wait for a reply, in BIOS ticks */
} Config;

/* Reads a simple KEY=VALUE file (see HACONFIG.INI in this directory for the
 * format). Returns 0 on success, -1 if the file couldn't be opened. Missing
 * keys keep sensible defaults (COM1, 9600 baud, 5s timeout). */
int config_load(const char *path, Config *cfg);

#endif
