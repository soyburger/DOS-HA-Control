#ifndef SCREEN_H
#define SCREEN_H

#include "protocol.h"

/* DOS Shell-style blue TUI helpers built on conio.h -- no Turbo Vision
 * dependency, so it builds with a plain OpenWatcom DOS target. */

void scr_init(void);       /* clear screen, set default colors, hide cursor */
void scr_shutdown(void);   /* restore cursor, clear screen on exit */

/* Draws everything that doesn't change between keypresses (background,
 * title bar, box borders) -- call this once after connecting, not on
 * every redraw. Use scr_set_hint for the status bar text after that. */
void scr_draw_chrome(const char *title, const char *status_left,
                      const char *status_right);

/* Rewrites just the status bar row -- cheap enough for every keypress. */
void scr_set_hint(const char *status_left, const char *status_right);

/* Draws the entity list inside its box and highlights `selected`.
 * Returns nothing; call scr_draw_chrome first. */
void scr_draw_list(const Entity *entities, int count, int selected);

/* Which field is Left/Right-selected in the options panel, in the order
 * they're drawn left to right. Only fields the entity actually supports
 * are ever focusable -- see main.c's cycle_focus. */
typedef enum {
    OPT_POWER = 0,
    OPT_BRIGHTNESS = 1,
    OPT_TEMP = 2,
    OPT_R = 3,
    OPT_G = 4,
    OPT_B = 5
} OptionKind;

/* Draws Power (always) and Brightness/Color Temp/R/G/B (only for fields
 * entity `e` supports) as single-column bar graphs, with `focused`
 * highlighted (Up/Down adjusts it live -- there's no separate edit mode).
 * Call after scr_draw_chrome and scr_draw_list. */
void scr_draw_options(const Entity *e, int focused);

/* Bottom-line transient message, e.g. "Connecting...", "ERR: ...". */
void scr_status_msg(const char *msg);

/* Simple modal message box; waits for any key. */
void scr_alert(const char *title, const char *msg);

/* Interactive COM port / baud picker, shown at startup so port settings
 * never require hand-editing HACONFIG.INI. *com_port is 0-3 (COM1-COM4);
 * *baud_index is 0-3 selecting {1200,2400,4800,9600}. Both are read as the
 * initial selection (e.g. loaded from HACONFIG.INI) and written back with
 * the user's choice when they press Enter. Left/Right change the COM
 * port, Up/Down change the baud rate. */
void scr_settings(int *com_port, int *baud_index);

#endif
