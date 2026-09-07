#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "config.h"
#include "net_serial.h"

#if defined(__WATCOMC__) && !defined(stricmp)
#define stricmp _stricmp
#endif

static void trim(char *s)
{
    int len = (int) strlen(s);
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n' ||
                        s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[--len] = '\0';
    }
}

static int baud_code_from_int(int baud)
{
    switch (baud) {
        case 1200: return SERIAL_BAUD_1200;
        case 2400: return SERIAL_BAUD_2400;
        case 4800: return SERIAL_BAUD_4800;
        default:   return SERIAL_BAUD_9600;
    }
}

int config_load(const char *path, Config *cfg)
{
    FILE *f;
    char line[128];

    /* Defaults */
    cfg->com_port = SERIAL_COM1;
    cfg->baud_code = SERIAL_BAUD_9600;
    cfg->timeout_ticks = 90; /* ~5 sec */

    f = fopen(path, "r");
    if (!f)
        return -1;

    while (fgets(line, sizeof(line), f)) {
        char *eq, *key, *val;

        trim(line);
        if (line[0] == '\0' || line[0] == ';' || line[0] == '#')
            continue;

        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        key = line;
        val = eq + 1;

        if (stricmp(key, "COM") == 0) {
            cfg->com_port = atoi(val) - 1; /* file uses 1-based COM1..4 */
            if (cfg->com_port < 0) cfg->com_port = 0;
            if (cfg->com_port > 3) cfg->com_port = 3;
        } else if (stricmp(key, "BAUD") == 0) {
            cfg->baud_code = baud_code_from_int(atoi(val));
        } else if (stricmp(key, "TIMEOUT") == 0) {
            cfg->timeout_ticks = atoi(val) * 18; /* seconds -> ticks */
        }
    }

    fclose(f);
    return 0;
}
