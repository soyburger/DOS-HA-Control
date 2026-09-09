#ifndef SCREEN_H
#define SCREEN_H

#include "protocol.h"

/* DOS Shell-style blue TUI helpers built on conio.h -- no Turbo Vision
 * dependency, so it builds with a plain OpenWatcom DOS target. */

void scr_init(void);       /* clear screen, set default colors, hide cursor */
void scr_shutdown(void);   /* restore cursor, clear screen on exit */

void scr_draw_chrome(const char *title, const char *status_left,
                      const char *status_right);

/* Draws the entity list inside its box and highlights `selected`.
 * Returns nothing; call scr_draw_chrome first. */
void scr_draw_list(const Entity *entities, int count, int selected);

/* Which field is Up/Down-selected in the options panel. Only fields the
 * entity actually supports are ever focusable -- see main.c's cycle_focus. */
typedef enum {
    OPT_POWER = 0,
    OPT_BRIGHTNESS = 1,
    OPT_R = 2,
    OPT_G = 3,
    OPT_B = 4,
    OPT_TEMP = 5
} OptionKind;

/* Draws Power (always) and Brightness/R/G/B/Color Temp (only for fields
 * entity `e` supports) as bar graphs, with `focused` highlighted (Left/
 * Right adjusts it live -- there's no separate edit mode). Call after
 * scr_draw_chrome and scr_draw_list. */
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
