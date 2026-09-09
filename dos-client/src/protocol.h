#ifndef PROTOCOL_H
#define PROTOCOL_H

#define PROTO_MAX_LINE   200
#define PROTO_MAX_ENTITIES 32
#define PROTO_ID_LEN     40
#define PROTO_NAME_LEN   32
#define PROTO_STATE_LEN  16

typedef struct {
    char entity_id[PROTO_ID_LEN];
    char friendly_name[PROTO_NAME_LEN];
    char state[PROTO_STATE_LEN];
    int brightness; /* 0-100, or -1 if this entity doesn't support it */
    int hue;         /* 0-359, or -1 if this entity doesn't support it */
} Entity;

/* Transport function pointers: >=0 bytes read/written on success, <0 on error. */
typedef int (*TransportReadLine)(char *buf, int buflen);
typedef int (*TransportWriteLine)(const char *line);

typedef struct {
    TransportReadLine read_line;
    TransportWriteLine write_line;
} Transport;

/* Returns 1 on PONG, 0 on failure/timeout. */
int proto_ping(Transport *t);

/* Fills entities[] (up to max) from a LIST reply. Returns count, or -1 on error. */
int proto_list(Transport *t, Entity *entities, int max);

/* Fills state (PROTO_STATE_LEN) for entity_id. Returns 0 on success, -1 on error. */
int proto_get(Transport *t, const char *entity_id, char *state);

/* cmd is "ON", "OFF", or "TOGGLE". Returns 0 on success, -1 on error.
   On error, err (if non-NULL) receives the ERR message text. */
int proto_service(Transport *t, const char *cmd, const char *entity_id,
                   char *err, int err_len);

/* hue: 0-359. Returns 0 on success, -1 on error (err filled as above). */
int proto_set_color(Transport *t, const char *entity_id, int hue,
                     char *err, int err_len);

/* pct: 0-100. Returns 0 on success, -1 on error (err filled as above). */
int proto_set_brightness(Transport *t, const char *entity_id, int pct,
                          char *err, int err_len);

#endif
