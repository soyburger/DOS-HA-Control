/* HACLIENT -- DOS Shell-style Home Assistant control panel for DOS 6.22 /
 * 486 machines. Talks to bridge/ha_bridge.py over a serial cable;
 * see docs/PROTOCOL.md and dos-client/docs/BUILD.md.
 */
#include <conio.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "config.h"
#include "protocol.h"
#include "screen.h"
#include "net_serial.h"

#define KEY_UP    72
#define KEY_DOWN  80
#define KEY_LEFT  75
#define KEY_RIGHT 77
#define KEY_ESC   27
#define KEY_ENTER 13

#define MODE_LIST    0
#define MODE_OPTIONS 1

static Entity entities[PROTO_MAX_ENTITIES];
static int entity_count = 0;
static int selected = 0;
static int focused = OPT_POWER;
static int mode = MODE_LIST;
static Transport transport;
static Config cfg;

/* Keeps `focused` valid for whichever entity is currently selected -- e.g.
 * landing on R/G/B for a color light, then backing out and picking a
 * plain switch, should fall back to Power rather than pointing at a
 * field that entity doesn't have. */
static void clamp_focus(void)
{
    Entity *e;

    if (entity_count == 0) {
        focused = OPT_POWER;
        return;
    }
    e = &entities[selected];
    if (focused == OPT_BRIGHTNESS && e->brightness < 0) focused = OPT_POWER;
    if ((focused == OPT_R || focused == OPT_G || focused == OPT_B) && e->r < 0)
        focused = OPT_POWER;
    if (focused == OPT_TEMP && e->min_k < 0) focused = OPT_POWER;
}

static int field_supported(const Entity *e, int f)
{
    switch (f) {
        case OPT_POWER:      return 1;
        case OPT_BRIGHTNESS: return e->brightness >= 0;
        case OPT_R:
        case OPT_G:
        case OPT_B:          return e->r >= 0;
        case OPT_TEMP:       return e->min_k >= 0;
        default:              return 0;
    }
}

/* Cycles `focused` forward (dir=1) or backward (dir=-1) among the fields
 * the current entity actually supports. */
static void cycle_focus(int dir)
{
    int i;

    if (entity_count == 0)
        return;

    for (i = 0; i < 6; i++) {
        focused = (focused + dir + 6) % 6;
        if (field_supported(&entities[selected], focused))
            return;
    }
}

static int connect_transport(void)
{
    scr_status_msg("Opening COM port...");
    if (serial_open(cfg.com_port, cfg.baud_code, cfg.timeout_ticks) != 0) {
        scr_alert("Port Error",
                  "Configured COM port was not detected or is unavailable.\n"
                  "Check your HACONFIG.INI settings.");
        return -1;
    }
    serial_get_transport(&transport);

    scr_status_msg("Pinging bridge...");
    if (!proto_ping(&transport)) {
        scr_alert("Link Error",
                   "Bridge did not respond to PING. Check the bridge is running\n"
                   "and HACONFIG.INI settings are correct.");
        return -1;
    }

    return 0;
}

static int refresh_list(void)
{
    int n;

    scr_status_msg("Refreshing device list...");
    n = proto_list(&transport, entities, PROTO_MAX_ENTITIES);
    if (n < 0) {
        scr_alert("Link Error", "Failed to fetch device list from bridge.");
        return -1;
    }
    entity_count = n;
    if (selected >= entity_count)
        selected = entity_count > 0 ? entity_count - 1 : 0;
    clamp_focus();
    return 0;
}

/* Chrome (background/title bar/box borders) is drawn once in main() --
 * repainting all 25 rows on every keypress is what caused the visible
 * flicker. redraw() only ever touches the cells that actually changed:
 * the status bar hint, the list, and the options panel. */
static void redraw(void)
{
    if (mode == MODE_LIST) {
        scr_set_hint("Up/Down: Select  Enter: Options  R: Refresh  Esc: Quit",
                     "[SERIAL]");
    } else {
        scr_set_hint("Left/Right: Field  Up/Down: Adjust  Enter: Set  Esc: Back",
                     "[SERIAL]");
    }
    scr_draw_list(entities, entity_count, selected);
    if (entity_count > 0)
        scr_draw_options(&entities[selected], focused);
}

/* Up/Down adjust the focused field's value on screen only -- no command is
 * sent per keypress, since a 9600-baud link is too slow for that. Enter
 * (do_commit) is what actually transmits the pending value. */
static void adjust_focused(int delta)
{
    Entity *e = &entities[selected];

    if (entity_count == 0)
        return;

    switch (focused) {
        case OPT_POWER:
            strcpy(e->state, delta > 0 ? "on" : "off");
            break;
        case OPT_BRIGHTNESS:
            e->brightness += delta * 10;
            if (e->brightness < 0) e->brightness = 0;
            if (e->brightness > 100) e->brightness = 100;
            break;
        case OPT_R:
            e->r += delta * 15;
            if (e->r < 0) e->r = 0;
            if (e->r > 255) e->r = 255;
            break;
        case OPT_G:
            e->g += delta * 15;
            if (e->g < 0) e->g = 0;
            if (e->g > 255) e->g = 255;
            break;
        case OPT_B:
            e->b += delta * 15;
            if (e->b < 0) e->b = 0;
            if (e->b > 255) e->b = 255;
            break;
        case OPT_TEMP:
            e->temp_k += delta * 100;
            if (e->temp_k < e->min_k) e->temp_k = e->min_k;
            if (e->temp_k > e->max_k) e->temp_k = e->max_k;
            break;
    }
}

static void do_commit(void)
{
    char err[64];
    Entity *e = &entities[selected];

    if (entity_count == 0)
        return;

    if (focused == OPT_POWER) {
        const char *cmd = strcmp(e->state, "on") == 0 ? "ON" : "OFF";
        scr_status_msg("Setting power...");
        if (proto_service(&transport, cmd, e->entity_id, err, sizeof(err)) != 0)
            scr_alert("Command Failed", err);
        return;
    }

    if (focused == OPT_BRIGHTNESS) {
        scr_status_msg("Setting brightness...");
        if (proto_set_brightness(&transport, e->entity_id, e->brightness,
                                  err, sizeof(err)) != 0)
            scr_alert("Command Failed", err);
        return;
    }

    if (focused == OPT_R || focused == OPT_G || focused == OPT_B) {
        scr_status_msg("Setting color...");
        if (proto_set_rgb(&transport, e->entity_id, e->r, e->g, e->b,
                           err, sizeof(err)) != 0)
            scr_alert("Command Failed", err);
        return;
    }

    if (focused == OPT_TEMP) {
        scr_status_msg("Setting color temp...");
        if (proto_set_temp(&transport, e->entity_id, e->temp_k,
                            err, sizeof(err)) != 0)
            scr_alert("Command Failed", err);
    }
}

int main(void)
{
    int baud_index;

    if (config_load("HACONFIG.INI", &cfg) != 0) {
        fprintf(stderr, "HACONFIG.INI not found in current directory; "
                         "using defaults.\n");
    }

    /* Config stores baud as the raw INT14h code (SERIAL_BAUD_1200..
     * SERIAL_BAUD_9600, i.e. 4..7); the settings screen works with a
     * plain 0-3 index into {1200,2400,4800,9600}. */
    baud_index = cfg.baud_code - SERIAL_BAUD_1200;
    if (baud_index < 0 || baud_index > 3)
        baud_index = 3; /* default 9600 */

    scr_init();
    scr_settings(&cfg.com_port, &baud_index);
    cfg.baud_code = SERIAL_BAUD_1200 + baud_index;

    if (connect_transport() != 0) {
        scr_shutdown();
        return 1;
    }

    if (refresh_list() != 0) {
        scr_shutdown();
        return 1;
    }

    scr_draw_chrome("Home Assistant Control", "", "[SERIAL]");
    redraw();

    for (;;) {
        int c = getch();

        if (c == 0 || c == 224) { /* extended key prefix */
            c = getch();

            if (mode == MODE_LIST) {
                if (c == KEY_UP) {
                    if (entity_count > 0)
                        selected = (selected + entity_count - 1) % entity_count;
                    clamp_focus();
                    redraw();
                } else if (c == KEY_DOWN) {
                    if (entity_count > 0)
                        selected = (selected + 1) % entity_count;
                    clamp_focus();
                    redraw();
                }
            } else { /* MODE_OPTIONS */
                if (c == KEY_UP) {
                    adjust_focused(1);
                    redraw();
                } else if (c == KEY_DOWN) {
                    adjust_focused(-1);
                    redraw();
                } else if (c == KEY_LEFT) {
                    cycle_focus(-1);
                    redraw();
                } else if (c == KEY_RIGHT) {
                    cycle_focus(1);
                    redraw();
                }
            }
            continue;
        }

        if (c == KEY_ENTER) {
            if (mode == MODE_LIST) {
                if (entity_count > 0) {
                    mode = MODE_OPTIONS;
                    clamp_focus();
                }
            } else {
                do_commit();
            }
            redraw();
        } else if (c == 'r' || c == 'R') {
            refresh_list();
            redraw();
        } else if (c == KEY_ESC) {
            if (mode == MODE_OPTIONS) {
                mode = MODE_LIST;
                redraw();
            } else {
                break;
            }
        }
    }

    scr_shutdown();
    return 0;
}
