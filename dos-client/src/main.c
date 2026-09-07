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
#define KEY_ESC   27
#define KEY_ENTER 13

static Entity entities[PROTO_MAX_ENTITIES];
static int entity_count = 0;
static int selected = 0;
static Transport transport;
static Config cfg;

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
    return 0;
}

static void redraw(void)
{
    scr_draw_chrome("Home Assistant Control - HACLIENT",
                     "Arrows: Move  Enter: Toggle  R: Refresh  Esc: Quit",
                     "[SERIAL]");
    scr_draw_list(entities, entity_count, selected);
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

int main(void)
{
    if (config_load("HACONFIG.INI", &cfg) != 0) {
        fprintf(stderr, "HACONFIG.INI not found in current directory; "
                         "using defaults.\n");
    }

    scr_init();

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
                if (selected > 0) selected--;
                redraw();
            } else if (c == KEY_DOWN) {
                if (selected < entity_count - 1) selected++;
                redraw();
            }
            continue;
        }

        if (c == KEY_ENTER) {
            do_toggle();
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
