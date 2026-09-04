/* DOS Shell-look text UI: white-on-blue chrome, single-line box drawing
 * (CP437), a highlighted list, and a status line. Built entirely on
 * conio.h so it needs no external TUI library.
 */
#include <conio.h>
#include <string.h>
#include <stdio.h>
#include "screen.h"

#define SCR_ROWS 25
#define SCR_COLS 80

#define LIST_TOP    4
#define LIST_BOTTOM (SCR_ROWS - 3)
#define LIST_LEFT   2
#define LIST_RIGHT  (SCR_COLS - 3)

/* CP437 box-drawing characters */
#define CH_TL 201  /* double top-left corner */
#define CH_TR 187
#define CH_BL 200
#define CH_BR 188
#define CH_H  205
#define CH_V  186
#define CH_TEE_L 204
#define CH_TEE_R 185

#define COL_BG     BLUE
#define COL_FG     WHITE
#define COL_TITLE  YELLOW
#define COL_SEL_BG CYAN
#define COL_SEL_FG BLACK
#define COL_ON     LIGHTGREEN
#define COL_OFF    LIGHTRED
#define COL_STATUS LIGHTGRAY

static void put_at(int x, int y, int fg, int bg, const char *s)
{
    textcolor(fg);
    textbackground(bg);
    gotoxy(x, y);
    cputs(s);
}

static void hline(int x1, int x2, int y, int fg, int bg)
{
    int x;
    textcolor(fg);
    textbackground(bg);
    for (x = x1; x <= x2; x++) {
        gotoxy(x, y);
        putch(CH_H);
    }
}

static void box(int x1, int y1, int x2, int y2, const char *title)
{
    int y;

    textcolor(COL_FG);
    textbackground(COL_BG);

    gotoxy(x1, y1); putch(CH_TL);
    hline(x1 + 1, x2 - 1, y1, COL_FG, COL_BG);
    gotoxy(x2, y1); putch(CH_TR);

    for (y = y1 + 1; y < y2; y++) {
        gotoxy(x1, y); putch(CH_V);
        gotoxy(x2, y); putch(CH_V);
    }

    gotoxy(x1, y2); putch(CH_BL);
    hline(x1 + 1, x2 - 1, y2, COL_FG, COL_BG);
    gotoxy(x2, y2); putch(CH_BR);

    if (title && *title) {
        char buf[SCR_COLS];
        sprintf(buf, " %s ", title);
        textcolor(COL_TITLE);
        gotoxy(x1 + 2, y1);
        cputs(buf);
    }
}

void scr_init(void)
{
    _setcursortype(_NOCURSOR);
    textbackground(COL_BG);
    textcolor(COL_FG);
    clrscr();
}

void scr_shutdown(void)
{
    textbackground(BLACK);
    textcolor(LIGHTGRAY);
    clrscr();
    _setcursortype(_NORMALCURSOR);
    gotoxy(1, 1);
}

void scr_draw_chrome(const char *title, const char *status_left,
                      const char *status_right)
{
    char line[SCR_COLS + 1];
    int pad;

    textbackground(COL_BG);
    textcolor(COL_FG);
    clrscr();

    /* Title bar */
    textbackground(CYAN);
    textcolor(BLACK);
    gotoxy(1, 1);
    memset(line, ' ', SCR_COLS);
    line[SCR_COLS] = '\0';
    cputs(line);
    pad = (SCR_COLS - (int) strlen(title)) / 2;
    if (pad < 0) pad = 0;
    gotoxy(pad + 1, 1);
    cputs(title);

    box(LIST_LEFT - 1, LIST_TOP - 1, LIST_RIGHT + 1, LIST_BOTTOM + 1,
        "Home Assistant Devices");

    /* Status bar */
    textbackground(CYAN);
    textcolor(BLACK);
    gotoxy(1, SCR_ROWS);
    memset(line, ' ', SCR_COLS);
    line[SCR_COLS] = '\0';
    cputs(line);
    gotoxy(2, SCR_ROWS);
    cputs(status_left);
    if (status_right) {
        gotoxy(SCR_COLS - (int) strlen(status_right) - 1, SCR_ROWS);
        cputs(status_right);
    }
}

void scr_draw_list(const Entity *entities, int count, int selected)
{
    int row, i;
    int visible = LIST_BOTTOM - LIST_TOP; /* rows available */

    for (row = 0; row < visible; row++) {
        i = row; /* no scrolling yet -- fine for a handful of entities */
        gotoxy(LIST_LEFT, LIST_TOP + row);

        if (i >= count) {
            textbackground(COL_BG);
            textcolor(COL_BG);
            cputs("                                                              ");
            continue;
        }

        {
            int fg = (i == selected) ? COL_SEL_FG : COL_FG;
            int bg = (i == selected) ? COL_SEL_BG : COL_BG;
            char namebuf[40];
            int statecol = (strcmp(entities[i].state, "on") == 0) ? COL_ON : COL_OFF;

            textbackground(bg);
            textcolor(fg);
            sprintf(namebuf, " %-38s", entities[i].friendly_name);
            cputs(namebuf);

            textcolor((i == selected) ? fg : statecol);
            {
                char stbuf[20];
                sprintf(stbuf, "%-20s", entities[i].state);
                cputs(stbuf);
            }
        }
    }
}

void scr_status_msg(const char *msg)
{
    char line[SCR_COLS + 1];

    textbackground(CYAN);
    textcolor(BLACK);
    gotoxy(2, SCR_ROWS);
    memset(line, ' ', 60);
    line[60] = '\0';
    cputs(line);
    gotoxy(2, SCR_ROWS);
    cputs(msg);
}

void scr_alert(const char *title, const char *msg)
{
    int x1 = 15, y1 = 10, x2 = 65, y2 = 14;

    box(x1, y1, x2, y2, title);
    textbackground(COL_BG);
    textcolor(LIGHTRED);
    gotoxy(x1 + 2, y1 + 2);
    cputs(msg);
    textcolor(WHITE);
    gotoxy(x1 + 2, y2 - 1);
    cputs("Press any key...");
    getch();
}
