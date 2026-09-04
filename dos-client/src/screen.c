/* DOS Shell-look text UI: white-on-blue chrome, single-line box drawing
 * (CP437), a highlighted list, and a status line.
 *
 * Writes directly to text-mode video memory at B800:0000 rather than using
 * Borland's textcolor()/gotoxy()/clrscr() -- OpenWatcom's conio.h doesn't
 * provide those (they're a Borland/Turbo C extension); direct video writes
 * plus BIOS INT 10h for the cursor are standard, compiler-independent DOS
 * technique and were verified to compile clean under OpenWatcom.
 */
#include <dos.h>
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

/* Standard 4-bit CGA/EGA/VGA text attribute palette (index, not RGB). */
#define BLACK        0
#define BLUE         1
#define GREEN        2
#define CYAN         3
#define RED          4
#define MAGENTA      5
#define BROWN        6
#define LIGHTGRAY    7
#define DARKGRAY     8
#define LIGHTBLUE    9
#define LIGHTGREEN   10
#define LIGHTCYAN    11
#define LIGHTRED     12
#define LIGHTMAGENTA 13
#define YELLOW       14
#define WHITE        15

#define ATTR(fg, bg) (unsigned char) (((bg) << 4) | (fg))

#define COL_BG     BLUE
#define COL_FG     WHITE
#define COL_TITLE  YELLOW
#define COL_SEL_BG CYAN
#define COL_SEL_FG BLACK
#define COL_ON     LIGHTGREEN
#define COL_OFF    LIGHTRED

static unsigned char far *video = (unsigned char far *) 0xB8000000L;

static void vputc(int row, int col, int ch, unsigned char attr)
{
    long off = ((long) row * SCR_COLS + col) * 2;
    video[off]     = (unsigned char) ch;
    video[off + 1] = attr;
}

static void vputs(int row, int col, const char *s, unsigned char attr)
{
    while (*s)
        vputc(row, col++, (unsigned char) *s++, attr);
}

static void vfill(int row, int col, int len, int ch, unsigned char attr)
{
    int i;
    for (i = 0; i < len; i++)
        vputc(row, col + i, ch, attr);
}

static void vfill_row(int row, unsigned char attr)
{
    vfill(row, 0, SCR_COLS, ' ', attr);
}

static void set_cursor_visible(int visible)
{
    union REGS r;
    r.h.ah = 0x01;
    if (visible) {
        r.h.ch = 0x0D;
        r.h.cl = 0x0E;
    } else {
        r.h.ch = 0x20;
        r.h.cl = 0x00;
    }
    int86(0x10, &r, &r);
}

static void set_cursor_pos(int row, int col)
{
    union REGS r;
    r.h.ah = 0x02;
    r.h.bh = 0;
    r.h.dh = (unsigned char) row;
    r.h.dl = (unsigned char) col;
    int86(0x10, &r, &r);
}

static void box(int x1, int y1, int x2, int y2, const char *title)
{
    int x, y;

    vputc(y1, x1, CH_TL, ATTR(COL_FG, COL_BG));
    for (x = x1 + 1; x < x2; x++)
        vputc(y1, x, CH_H, ATTR(COL_FG, COL_BG));
    vputc(y1, x2, CH_TR, ATTR(COL_FG, COL_BG));

    for (y = y1 + 1; y < y2; y++) {
        vputc(y, x1, CH_V, ATTR(COL_FG, COL_BG));
        vputc(y, x2, CH_V, ATTR(COL_FG, COL_BG));
    }

    vputc(y2, x1, CH_BL, ATTR(COL_FG, COL_BG));
    for (x = x1 + 1; x < x2; x++)
        vputc(y2, x, CH_H, ATTR(COL_FG, COL_BG));
    vputc(y2, x2, CH_BR, ATTR(COL_FG, COL_BG));

    if (title && *title) {
        char buf[SCR_COLS];
        sprintf(buf, " %s ", title);
        vputs(y1, x1 + 2, buf, ATTR(COL_TITLE, COL_BG));
    }
}

void scr_init(void)
{
    int r;

    set_cursor_visible(0);
    for (r = 0; r < SCR_ROWS; r++)
        vfill_row(r, ATTR(COL_FG, COL_BG));
}

void scr_shutdown(void)
{
    int r;
    for (r = 0; r < SCR_ROWS; r++)
        vfill_row(r, ATTR(LIGHTGRAY, BLACK));
    set_cursor_pos(0, 0);
    set_cursor_visible(1);
}

void scr_draw_chrome(const char *title, const char *status_left,
                      const char *status_right)
{
    int r;
    int pad;

    for (r = 0; r < SCR_ROWS; r++)
        vfill_row(r, ATTR(COL_FG, COL_BG));

    /* Title bar */
    vfill_row(0, ATTR(BLACK, CYAN));
    pad = (SCR_COLS - (int) strlen(title)) / 2;
    if (pad < 0) pad = 0;
    vputs(0, pad, title, ATTR(BLACK, CYAN));

    box(LIST_LEFT - 1, LIST_TOP - 1, LIST_RIGHT + 1, LIST_BOTTOM + 1,
        "Home Assistant Devices");

    /* Status bar */
    vfill_row(SCR_ROWS - 1, ATTR(BLACK, CYAN));
    vputs(SCR_ROWS - 1, 1, status_left, ATTR(BLACK, CYAN));
    if (status_right) {
        int x = SCR_COLS - (int) strlen(status_right) - 2;
        vputs(SCR_ROWS - 1, x, status_right, ATTR(BLACK, CYAN));
    }
}

void scr_draw_list(const Entity *entities, int count, int selected)
{
    int row, i;
    int visible = LIST_BOTTOM - LIST_TOP;

    for (row = 0; row < visible; row++) {
        i = row; /* no scrolling yet -- fine for a handful of entities */

        if (i >= count) {
            vfill(LIST_TOP + row, LIST_LEFT, LIST_RIGHT - LIST_LEFT, ' ',
                  ATTR(COL_BG, COL_BG));
            continue;
        }

        {
            unsigned char fg = (i == selected) ? COL_SEL_FG : COL_FG;
            unsigned char bg = (i == selected) ? COL_SEL_BG : COL_BG;
            unsigned char rowattr = ATTR(fg, bg);
            unsigned char stateattr;
            char namebuf[40];
            char stbuf[20];

            stateattr = (i == selected) ? rowattr
                        : ATTR(strcmp(entities[i].state, "on") == 0
                                   ? COL_ON : COL_OFF, COL_BG);

            sprintf(namebuf, " %-38s", entities[i].friendly_name);
            vputs(LIST_TOP + row, LIST_LEFT, namebuf, rowattr);

            sprintf(stbuf, "%-20s", entities[i].state);
            vputs(LIST_TOP + row, LIST_LEFT + 39, stbuf, stateattr);
        }
    }
}

void scr_status_msg(const char *msg)
{
    vfill(SCR_ROWS - 1, 1, 60, ' ', ATTR(BLACK, CYAN));
    vputs(SCR_ROWS - 1, 1, msg, ATTR(BLACK, CYAN));
}

void scr_alert(const char *title, const char *msg)
{
    int x1 = 15, y1 = 10, x2 = 65, y2 = 14;

    box(x1, y1, x2, y2, title);
    vfill(y1 + 2, x1 + 2, x2 - x1 - 4, ' ', ATTR(LIGHTRED, COL_BG));
    vputs(y1 + 2, x1 + 2, msg, ATTR(LIGHTRED, COL_BG));
    vputs(y2 - 1, x1 + 2, "Press any key...", ATTR(WHITE, COL_BG));
    getch();
}
