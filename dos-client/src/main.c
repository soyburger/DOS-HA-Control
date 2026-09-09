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
#define KEY_SHIFT_TAB 15
#define KEY_ESC   27
#define KEY_ENTER 13
#define KEY_TAB   9

static Entity entities[PROTO_MAX_ENTITIES];
static int entity_count = 0;
static int selected = 0;
static int focused = OPT_POWER;
static Transport transport;
static Config cfg;

/* Keeps `focused` valid for whichever entity is currently selected -- e.g.
 * landing on Color for a light, then Tab-ing to a plain switch, should
 * fall back to Power rather than pointing at an option that entity
 * doesn't have. */
static void clamp_focus(void)
{
    if (entity_count == 0) {
        focused = OPT_POWER;
        return;
    }
    if (focused == OPT_COLOR && entities[selected].hue < 0)
        focused = OPT_POWER;
    if (focused == OPT_BRIGHTNESS && entities[selected].brightness < 0)
        focused = OPT_POWER;
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

static void redraw(void)
{
    scr_draw_chrome("Home Assistant Control - HACLIENT",
                     "Tab: Light  L/R: Option  U/D: Adjust  Enter: Set  Esc: Quit",
                     "[SERIAL]");
    scr_draw_list(entities, entity_count, selected);
    if (entity_count > 0)
        scr_draw_options(&entities[selected], focused);
}

/* Cycles `focused` forward (dir=1) or backward (dir=-1) among the options
 * the current entity actually supports -- Color/Brightness are skipped
 * for a plain switch, matching what scr_draw_options chooses to show. */
static void cycle_focus(int dir)
{
    int i;

    if (entity_count == 0)
        return;

    for (i = 0; i < 3; i++) {
        focused = (focused + dir + 3) % 3;
        if (focused == OPT_POWER) return;
        if (focused == OPT_COLOR && entities[selected].hue >= 0) return;
        if (focused == OPT_BRIGHTNESS && entities[selected].brightness >= 0) return;
    }
}

static void do_toggle(void)
{
    char err[64];

    if (entity_count == 0)
        return;

    scr_status_msg("Sending TOGGLE...");
    if (proto_service(&transport, "TOGGLE", entities[selected].entity_id,
                       err, sizeof(err)) != 0) {
        scr_alert("Command Failed", err);
        return;
    }

    /* Optimistic flip; a refresh (R) will resync if HA disagrees. */
    strcpy(entities[selected].state,
           strcmp(entities[selected].state, "on") == 0 ? "off" : "on");
}

/* Up/Down adjust the focused option's value on screen only -- no command
 * is sent per keypress, since a 9600-baud link is too slow for that.
 * Enter (do_commit) is what actually transmits the pending value. */
static void adjust_focused(int delta)
{
    Entity *e = &entities[selected];

    if (entity_count == 0)
        return;

    if (focused == OPT_COLOR && e->hue >= 0) {
        e->hue = (e->hue + delta * 15 + 360) % 360;
    } else if (focused == OPT_BRIGHTNESS && e->brightness >= 0) {
        e->brightness += delta * 10;
        if (e->brightness < 0) e->brightness = 0;
        if (e->brightness > 100) e->brightness = 100;
    }
}

static void do_commit(void)
{
    char err[64];
    Entity *e = &entities[selected];

    if (entity_count == 0)
        return;

    if (focused == OPT_POWER) {
        do_toggle();
        return;
    }

    if (focused == OPT_COLOR) {
        scr_status_msg("Setting color...");
        if (proto_set_color(&transport, e->entity_id, e->hue, err, sizeof(err)) != 0)
            scr_alert("Command Failed", err);
        return;
    }

    if (focused == OPT_BRIGHTNESS) {
        scr_status_msg("Setting brightness...");
        if (proto_set_brightness(&transport, e->entity_id, e->brightness,
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

    redraw();

    for (;;) {
        int c = getch();

        if (c == 0 || c == 224) { /* extended key prefix */
            c = getch();
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
            } else if (c == KEY_SHIFT_TAB) {
                if (entity_count > 0)
                    selected = (selected + entity_count - 1) % entity_count;
                clamp_focus();
                redraw();
            }
            continue;
        }

        if (c == KEY_TAB) {
            if (entity_count > 0)
                selected = (selected + 1) % entity_count;
            clamp_focus();
            redraw();
        } else if (c == KEY_ENTER) {
            do_commit();
            redraw();
        } else if (c == 'r' || c == 'R') {
            refresh_list();
            redraw();
        } else if (c == KEY_ESC) {
            break;
        }
    }

    scr_shutdown();
    return 0;
}
