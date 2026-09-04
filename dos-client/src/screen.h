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

/* Bottom-line transient message, e.g. "Connecting...", "ERR: ...". */
void scr_status_msg(const char *msg);

/* Simple modal message box; waits for any key. */
void scr_alert(const char *title, const char *msg);

#endif
