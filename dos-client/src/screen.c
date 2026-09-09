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
#include <i86.h>
#include <string.h>
#include <stdio.h>
#include "screen.h"

#define SCR_ROWS 25
#define SCR_COLS 80

#define LIST_TOP    4
#define LIST_BOTTOM (SCR_ROWS - 8) /* leaves room for the options panel below */
#define LIST_LEFT   2
#define LIST_RIGHT  (SCR_COLS - 3)

#define OPT_TOP    (LIST_BOTTOM + 3)
#define OPT_BOTTOM (OPT_TOP + 3)

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

static unsigned char _far *video = (unsigned char _far *) _MK_FP(0xB800, 0x0000);

static void vputc(int row, int col, int ch, unsigned char attr)
{
    unsigned int off = ((unsigned int) row * SCR_COLS + col) * 2;
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
    box(LIST_LEFT - 1, OPT_TOP, LIST_RIGHT + 1, OPT_BOTTOM, "Options");

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
            char namebuf[64];
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

/* EGA/CGA text mode has 16 fixed colors, not a continuous spectrum -- this
 * picks whichever of a handful of "colorful" palette entries is nearest to
 * the given hue, purely for an on-screen preview swatch. The precise hue
 * value is always shown as text alongside it and is what actually gets
 * sent to Home Assistant, so the real light gets accurate color even
 * though the swatch can only approximate it. */
static int hue_to_color(int hue)
{
    static const int anchors[6] = { 0, 60, 120, 180, 240, 300 };
    static const int colors[6]  = { LIGHTRED, YELLOW, LIGHTGREEN, LIGHTCYAN,
                                     LIGHTBLUE, LIGHTMAGENTA };
    int best = 0, best_d = 361, i;

    for (i = 0; i < 6; i++) {
        int d = hue - anchors[i];
        if (d < 0) d = -d;
        if (d > 180) d = 360 - d;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return colors[best];
}

void scr_draw_options(const Entity *e, int focused)
{
    int y = OPT_TOP + 1;
    int col = LIST_LEFT + 1;
    char buf[24];

    vfill(y, LIST_LEFT, LIST_RIGHT - LIST_LEFT, ' ', ATTR(COL_BG, COL_BG));

    {
        unsigned char a = (focused == OPT_POWER)
                               ? ATTR(COL_SEL_FG, COL_SEL_BG) : ATTR(COL_FG, COL_BG);
        sprintf(buf, "Power: %-3s", strcmp(e->state, "on") == 0 ? "ON" : "OFF");
        vputs(y, col, buf, a);
        col += (int) strlen(buf) + 3;
    }

    if (e->hue >= 0) {
        unsigned char a = (focused == OPT_COLOR)
                               ? ATTR(COL_SEL_FG, COL_SEL_BG) : ATTR(COL_FG, COL_BG);

        vputs(y, col, "Color: ", a);
        col += 7;
        vputc(y, col, 219 /* solid block, CP437 */, ATTR(hue_to_color(e->hue), COL_BG));
        col += 1;
        sprintf(buf, " %3d\xf8 ", e->hue); /* \xf8 = CP437 degree symbol */
        vputs(y, col, buf, a);
        col += (int) strlen(buf) + 2;
    }

    if (e->brightness >= 0) {
        unsigned char a = (focused == OPT_BRIGHTNESS)
                               ? ATTR(COL_SEL_FG, COL_SEL_BG) : ATTR(COL_FG, COL_BG);
        sprintf(buf, "Brightness: %3d%%", e->brightness);
        vputs(y, col, buf, a);
    }
}

void scr_status_msg(const char *msg)
{
    vfill(SCR_ROWS - 1, 1, 60, ' ', ATTR(BLACK, CYAN));
    vputs(SCR_ROWS - 1, 1, msg, ATTR(BLACK, CYAN));
}

/* Word-wraps msg (honoring embedded '\n' as an explicit break) into the
 * alert box. Earlier this just vputs() the whole string on one row --
 * with no wrapping, a message longer than the box (or containing a literal
 * '\n', which isn't a real line break in video memory) would spill off the
 * screen and collide with the "Press any key" prompt, garbling both. */
void scr_alert(const char *title, const char *msg)
{
    int x1 = 12, y1 = 8, x2 = 68, y2 = 16;
    int width = x2 - x1 - 4;
    int max_row = y2 - 2;
    int row = y1 + 2;
    char line[128];
    const char *p = msg;
    int r;

    box(x1, y1, x2, y2, title);
    for (r = y1 + 1; r < y2; r++)
        vfill(r, x1 + 1, x2 - x1 - 1, ' ', ATTR(COL_BG, COL_BG));

    while (*p && row <= max_row) {
        int col = 0;
        int last_space = -1;

        while (p[col] && p[col] != '\n' && col < width) {
            if (p[col] == ' ')
                last_space = col;
            col++;
        }
        /* if we hit the width limit mid-word, back up to the last space so
         * we don't split a word across two lines */
        if (col == width && p[col] != '\0' && p[col] != '\n' && last_space >= 0)
            col = last_space;

        memcpy(line, p, col);
        line[col] = '\0';
        vputs(row, x1 + 2, line, ATTR(LIGHTRED, COL_BG));
        row++;

        p += col;
        while (*p == ' ')
            p++;
        if (*p == '\n')
            p++;
    }

    vputs(y2 - 1, x1 + 2, "Press any key...", ATTR(WHITE, COL_BG));
    getch();
}

static const char *BAUD_LABELS[4] = { "1200", "2400", "4800", "9600" };

#define SK_LEFT  75
#define SK_RIGHT 77
#define SK_UP    72
#define SK_DOWN  80
#define SK_ENTER 13

void scr_settings(int *com_port, int *baud_index)
{
    int x1 = 20, y1 = 8, x2 = 60, y2 = 15;

    for (;;) {
        char buf[40];
        int r;

        box(x1, y1, x2, y2, "Port Settings");
        for (r = y1 + 1; r < y2; r++)
            vfill(r, x1 + 1, x2 - x1 - 1, ' ', ATTR(COL_FG, COL_BG));

        sprintf(buf, "COM Port:   COM%d", *com_port + 1);
        vputs(y1 + 1, x1 + 2, buf, ATTR(COL_FG, COL_BG));
        vputs(y1 + 2, x1 + 2, "            (Left/Right to change)",
              ATTR(LIGHTGRAY, COL_BG));

        sprintf(buf, "Baud Rate:  %s", BAUD_LABELS[*baud_index]);
        vputs(y1 + 4, x1 + 2, buf, ATTR(COL_FG, COL_BG));
        vputs(y1 + 5, x1 + 2, "            (Up/Down to change)",
              ATTR(LIGHTGRAY, COL_BG));

        vputs(y2 - 1, x1 + 2, "Press ENTER to connect", ATTR(WHITE, COL_BG));

        {
            int c = getch();

            if (c == 0 || c == 224) {
                c = getch();
                if (c == SK_LEFT)
                    *com_port = (*com_port + 3) % 4;
                else if (c == SK_RIGHT)
                    *com_port = (*com_port + 1) % 4;
                else if (c == SK_UP)
                    *baud_index = (*baud_index + 1) % 4;
                else if (c == SK_DOWN)
                    *baud_index = (*baud_index + 3) % 4;
            } else if (c == SK_ENTER) {
                break;
            }
        }
    }
}
