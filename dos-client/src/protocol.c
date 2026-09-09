/* Transport-agnostic parser for the bridge line protocol.
 * See docs/PROTOCOL.md at the repo root for the wire format. Works
 * identically over mTCP (net_tcp.c) or a serial COM port (net_serial.c) --
 * this file only ever calls the Transport function pointers.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "protocol.h"

static char linebuf[PROTO_MAX_LINE + 2];

/* Splits "FIELD|FIELD|FIELD" into up to 'max' fields, in place (writes NULs
 * into src). Returns the number of fields found. */
static int split_pipes(char *src, char *fields[], int max)
{
    int n = 0;
    char *p = src;

    fields[n++] = p;
    while (*p && n < max) {
        if (*p == '|') {
            *p = '\0';
            fields[n++] = p + 1;
        }
        p++;
    }
    return n;
}

int proto_ping(Transport *t)
{
    if (t->write_line("PING") < 0)
        return 0;
    if (t->read_line(linebuf, sizeof(linebuf)) < 0)
        return 0;
    return strncmp(linebuf, "PONG", 4) == 0;
}

int proto_list(Transport *t, Entity *entities, int max)
{
    int count = 0;

    if (t->write_line("LIST") < 0)
        return -1;

    for (;;) {
        char *fields[6];
        int nf;

        if (t->read_line(linebuf, sizeof(linebuf)) < 0)
            return -1;

        if (strcmp(linebuf, "END") == 0)
            break;

        if (strncmp(linebuf, "ENTITY|", 7) != 0)
            continue; /* ignore anything unexpected, keep reading */

        if (count >= max)
            continue; /* keep draining until END even if we're full */

        nf = split_pipes(linebuf, fields, 6);
        if (nf < 6)
            continue;

        strncpy(entities[count].entity_id, fields[1], PROTO_ID_LEN - 1);
        entities[count].entity_id[PROTO_ID_LEN - 1] = '\0';
        strncpy(entities[count].friendly_name, fields[2], PROTO_NAME_LEN - 1);
        entities[count].friendly_name[PROTO_NAME_LEN - 1] = '\0';
        strncpy(entities[count].state, fields[3], PROTO_STATE_LEN - 1);
        entities[count].state[PROTO_STATE_LEN - 1] = '\0';
        entities[count].brightness = atoi(fields[4]);
        entities[count].hue = atoi(fields[5]);
        count++;
    }

    return count;
}

int proto_get(Transport *t, const char *entity_id, char *state)
{
    char cmd[PROTO_ID_LEN + 8];
    char *fields[3];
    int nf;

    sprintf(cmd, "GET %s", entity_id);
    if (t->write_line(cmd) < 0)
        return -1;
    if (t->read_line(linebuf, sizeof(linebuf)) < 0)
        return -1;

    if (strncmp(linebuf, "STATE|", 6) != 0)
        return -1;

    nf = split_pipes(linebuf, fields, 3);
    if (nf < 3)
        return -1;

    strncpy(state, fields[2], PROTO_STATE_LEN - 1);
    state[PROTO_STATE_LEN - 1] = '\0';
    return 0;
}

/* Sends `line` as-is, then expects "OK" or "ERR|<msg>" back. Shared by
 * proto_service/proto_set_color/proto_set_brightness -- they only differ
 * in how the command line itself gets built. */
static int send_and_check(Transport *t, const char *line, char *err, int err_len)
{
    if (t->write_line(line) < 0)
        return -1;
    if (t->read_line(linebuf, sizeof(linebuf)) < 0)
        return -1;

    if (strcmp(linebuf, "OK") == 0)
        return 0;

    if (err && err_len > 0) {
        if (strncmp(linebuf, "ERR|", 4) == 0)
            strncpy(err, linebuf + 4, err_len - 1);
        else
            strncpy(err, linebuf, err_len - 1);
        err[err_len - 1] = '\0';
    }
    return -1;
}

int proto_service(Transport *t, const char *cmd, const char *entity_id,
                   char *err, int err_len)
{
    char line[PROTO_ID_LEN + 8];

    sprintf(line, "%s %s", cmd, entity_id);
    return send_and_check(t, line, err, err_len);
}

int proto_set_color(Transport *t, const char *entity_id, int hue,
                     char *err, int err_len)
{
    char line[PROTO_ID_LEN + 20];

    sprintf(line, "SETCOLOR %s %d", entity_id, hue);
    return send_and_check(t, line, err, err_len);
}

int proto_set_brightness(Transport *t, const char *entity_id, int pct,
                          char *err, int err_len)
{
    char line[PROTO_ID_LEN + 20];

    sprintf(line, "SETBRIGHT %s %d", entity_id, pct);
    return send_and_check(t, line, err, err_len);
}
