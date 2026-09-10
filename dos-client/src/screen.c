/* DOS Shell-look text UI: magenta/yellow chrome, cyan canvas, a device
 * list box sized to fit its content, and an options panel that pops up
 * centered in front of it (with a drop shadow) when an entity is chosen.
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

#define POPUP_W 54
#define POPUP_H 18

/* CP437 box-drawing characters */
#define CH_TL 201  /* double top-left corner */
#define CH_TR 187
#define CH_BL 200
#define CH_BR 188
#define CH_H  205
#define CH_V  186
#define CH_BLOCK 219 /* solid block, used for bar fill */

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

/* Two rules, applied everywhere: text on magenta is always white, text on
 * cyan is always black. Yellow is reserved for small accents only (box
 * title captions) -- never body text. Anything not inside a window is
 * plain cyan canvas (except the title/status bars). */
#define COL_CANVAS  CYAN
#define COL_BG      MAGENTA  /* window fill, borders (white-on-magenta) */
#define COL_FG      WHITE    /* body text on COL_BG */
#define COL_TITLE   YELLOW   /* accent: box title captions only */
#define COL_ACCENT_BG MAGENTA  /* title bar / status bar */
#define COL_ACCENT_FG WHITE
#define COL_HILITE_BG CYAN   /* selected list row / current ON-OFF state */
#define COL_HILITE_FG BLACK  /* text on COL_HILITE_BG */
#define COL_FOCUS_BG  YELLOW /* which field Left/Right is on -- distinct from COL_HILITE */
#define COL_FOCUS_FG  BLACK  /* text on COL_FOCUS_BG */

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

/* Fills a rectangle (inclusive of both corners) with a blank space in the
 * given attribute -- used to clear a box's interior before redrawing it. */
static void vfill_rect(int x1, int y1, int x2, int y2, unsigned char attr)
{
    int y;
    for (y = y1; y <= y2; y++)
        vfill(y, x1, x2 - x1 + 1, ' ', attr);
}

/* Rewrites only the attribute byte of a cell, leaving its character alone
 * -- used for the popup's drop shadow, which darkens whatever's already
 * underneath rather than blanking it. */
static void vshade(int row, int col, unsigned char attr)
{
    unsigned int off;
    if (row < 0 || row >= SCR_ROWS || col < 0 || col >= SCR_COLS)
        return;
    off = ((unsigned int) row * SCR_COLS + col) * 2 + 1;
    video[off] = attr;
}

/* Classic Turbo Vision-style drop shadow: a solid dark strip offset 1 row
 * down and 2 columns right of the box's own footprint. */
static void draw_shadow(int x1, int y1, int x2, int y2)
{
    int x, y;
    for (y = y1 + 1; y <= y2 + 1; y++)
        for (x = x2 + 1; x <= x2 + 2; x++)
            vshade(y, x, ATTR(BLACK, BLACK));
    for (y = y2 + 1; y <= y2 + 1; y++)
        for (x = x1 + 2; x <= x2; x++)
            vshade(y, x, ATTR(BLACK, BLACK));
}

/* Vertical bar graph: `height` rows tall starting at (x, y_top), `width`
 * columns wide, filled from the bottom up to reflect value/[min,max]. */
static void vbar(int x, int y_top, int width, int height,
                  int value, int min_val, int max_val, unsigned char fill_attr)
{
    int range = max_val - min_val;
    int filled = (range > 0) ? ((value - min_val) * height) / range : 0;
    int row;

    if (filled < 0) filled = 0;
    if (filled > height) filled = height;

    for (row = 0; row < height; row++) {
        int from_bottom = height - 1 - row;
        int is_filled = from_bottom < filled;
        vfill(y_top + row, x, width, is_filled ? CH_BLOCK : ' ',
              is_filled ? fill_attr : ATTR(COL_FG, COL_BG));
    }
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
        vfill_row(r, ATTR(COL_FG, COL_CANVAS));
}

void scr_shutdown(void)
{
    int r;
    for (r = 0; r < SCR_ROWS; r++)
        vfill_row(r, ATTR(LIGHTGRAY, BLACK));
    set_cursor_pos(0, 0);
    set_cursor_visible(1);
}

/* Rewrites just the status bar row -- cheap enough to call on every
 * keypress without the flicker a full scr_draw_chrome() causes. */
void scr_set_hint(const char *status_left, const char *status_right)
{
    vfill_row(SCR_ROWS - 1, ATTR(COL_ACCENT_FG, COL_ACCENT_BG));
    vputs(SCR_ROWS - 1, 1, status_left, ATTR(COL_ACCENT_FG, COL_ACCENT_BG));
    if (status_right) {
        int x = SCR_COLS - (int) strlen(status_right) - 2;
        vputs(SCR_ROWS - 1, x, status_right, ATTR(COL_ACCENT_FG, COL_ACCENT_BG));
    }
}

/* Draws everything that doesn't change between keypresses: cyan canvas,
 * title bar. Call this once after connecting and again whenever the
 * options popup closes (to erase it and its shadow) -- not on every
 * redraw, since repainting all 25 rows every keystroke is what caused
 * the visible flicker. scr_draw_list/scr_draw_options/scr_set_hint only
 * ever touch the specific cells that actually changed. */
void scr_draw_chrome(const char *title, const char *status_left,
                      const char *status_right)
{
    int r;
    int pad;

    for (r = 0; r < SCR_ROWS; r++)
        vfill_row(r, ATTR(COL_FG, COL_CANVAS));

    vfill_row(0, ATTR(COL_ACCENT_FG, COL_ACCENT_BG));
    pad = (SCR_COLS - (int) strlen(title)) / 2;
    if (pad < 0) pad = 0;
    vputs(0, pad, title, ATTR(COL_ACCENT_FG, COL_ACCENT_BG));

    scr_set_hint(status_left, status_right);
}

#define LIST_TITLE "Home Assistant Devices"

/* Sized to fit its content: at least wide enough for the box title (plus
 * room for the border), or the longest entity name if that's wider; just
 * tall enough for the entity count. Centered on the canvas. */
void scr_draw_list(const Entity *entities, int count, int selected)
{
    int maxlen = 4;
    int i, x1, y1, x2, y2, w, h, width, title_w;

    for (i = 0; i < count; i++) {
        int len = (int) strlen(entities[i].friendly_name);
        if (len > maxlen) maxlen = len;
    }

    w = maxlen + 4;                    /* 1-space padding each side + border */
    title_w = (int) strlen(LIST_TITLE) + 6; /* " title " plus corners/margin */
    if (title_w > w) w = title_w;
    h = (count > 0 ? count : 1) + 2;   /* content rows + border */
    x1 = (SCR_COLS - w) / 2;
    y1 = 1 + ((SCR_ROWS - 2 - h) / 2);
    x2 = x1 + w - 1;
    y2 = y1 + h - 1;

    vfill_rect(x1 + 1, y1 + 1, x2 - 1, y2 - 1, ATTR(COL_FG, COL_BG));
    box(x1, y1, x2, y2, LIST_TITLE);

    width = x2 - x1 - 1;
    for (i = 0; i < count; i++) {
        unsigned char attr = (i == selected)
                                  ? ATTR(COL_HILITE_FG, COL_HILITE_BG)
                                  : ATTR(COL_FG, COL_BG);
        char namebuf[40];

        sprintf(namebuf, " %-*s", width - 1, entities[i].friendly_name);
        namebuf[width] = '\0';
        vputs(y1 + 1 + i, x1 + 1, namebuf, attr);
    }
}

/* Centers `s` within a `width`-wide field starting at column x. */
static void vputs_centered(int row, int x, int width, const char *s,
                            unsigned char attr)
{
    int len = (int) strlen(s);
    int pad = (width - len) / 2;
    if (pad < 0) pad = 0;
    vfill(row, x, width, ' ', attr);
    vputs(row, x + pad, s, attr);
}

/* Draws one bar-graph field: centered label, a single 1-wide centered
 * bar, centered value text below. x/width define the column this field
 * occupies (the bar itself is always 1 char wide -- no mix of wide and
 * narrow bars). popup_y1 is the enclosing popup's top row. */
static void draw_field(int x, int width, int popup_y1, const char *label,
                        int value, int min_val, int max_val,
                        const char *value_fmt, unsigned char fill_attr,
                        int focused)
{
    char buf[20];
    unsigned char label_attr = focused ? ATTR(COL_FOCUS_FG, COL_FOCUS_BG)
                                        : ATTR(COL_FG, COL_BG);
    int bar_x = x + width / 2;

    vputs_centered(popup_y1 + 2, x, width, label, label_attr);
    vbar(bar_x, popup_y1 + 4, 1, 10, value, min_val, max_val, fill_attr);
    sprintf(buf, value_fmt, value);
    vputs_centered(popup_y1 + 15, x, width, buf, label_attr);
}

/* Field order left to right: Power, Brightness, Color Temp, R/G/B --
 * matches the Left/Right cycle order in main.c so the highlighted field
 * always moves the direction the key implies. Pops up centered, on top
 * of whatever's already drawn (the device list), with a drop shadow. */
void scr_draw_options(const Entity *e, int focused)
{
    int x1 = (SCR_COLS - POPUP_W) / 2;
    int y1 = 1 + ((SCR_ROWS - 2 - POPUP_H) / 2);
    int x2 = x1 + POPUP_W - 1;
    int y2 = y1 + POPUP_H - 1;
    int x;

    draw_shadow(x1, y1, x2, y2);
    vfill_rect(x1 + 1, y1 + 1, x2 - 1, y2 - 1, ATTR(COL_FG, COL_BG));
    box(x1, y1, x2, y2, "Options");

    x = x1 + 2;

    /* Power is binary, not a range -- two stacked lines instead of a bar,
     * ON at the top of the bar zone and OFF at the bottom, so it's
     * immediately visible which one is currently active. */
    {
        int is_on = strcmp(e->state, "on") == 0;
        unsigned char label_attr = (focused == OPT_POWER)
                                        ? ATTR(COL_FOCUS_FG, COL_FOCUS_BG)
                                        : ATTR(COL_FG, COL_BG);
        unsigned char on_attr  = is_on  ? ATTR(COL_HILITE_FG, COL_HILITE_BG)
                                         : ATTR(COL_FG, COL_BG);
        unsigned char off_attr = !is_on ? ATTR(COL_HILITE_FG, COL_HILITE_BG)
                                         : ATTR(COL_FG, COL_BG);

        vputs_centered(y1 + 2, x, 8, "Power", label_attr);
        vputs_centered(y1 + 4, x, 8, "ON", on_attr);
        vputs_centered(y1 + 13, x, 8, "OFF", off_attr);
    }
    x += 10;

    if (e->brightness >= 0) {
        draw_field(x, 10, y1, "Brightness", e->brightness, 0, 100, "%d%%",
                   ATTR(WHITE, COL_BG), focused == OPT_BRIGHTNESS);
        x += 12;
    }

    if (e->min_k >= 0) {
        int mid = (e->min_k + e->max_k) / 2;
        unsigned char temp_fill = ATTR(e->temp_k < mid ? YELLOW : LIGHTCYAN, COL_BG);
        draw_field(x, 10, y1, "Color Temp", e->temp_k, e->min_k, e->max_k, "%dK",
                   temp_fill, focused == OPT_TEMP);
        x += 12;
    }

    if (e->r >= 0) {
        draw_field(x, 4, y1, "R", e->r, 0, 255, "%d",
                   ATTR(LIGHTRED, COL_BG), focused == OPT_R);
        x += 5;
        draw_field(x, 4, y1, "G", e->g, 0, 255, "%d",
                   ATTR(LIGHTGREEN, COL_BG), focused == OPT_G);
        x += 5;
        draw_field(x, 4, y1, "B", e->b, 0, 255, "%d",
                   ATTR(LIGHTBLUE, COL_BG), focused == OPT_B);
    }
}

void scr_status_msg(const char *msg)
{
    vfill(SCR_ROWS - 1, 1, 60, ' ', ATTR(COL_ACCENT_FG, COL_ACCENT_BG));
    vputs(SCR_ROWS - 1, 1, msg, ATTR(COL_ACCENT_FG, COL_ACCENT_BG));
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

    draw_shadow(x1, y1, x2, y2);
    box(x1, y1, x2, y2, title);
    for (r = y1 + 1; r < y2; r++)
        vfill(r, x1 + 1, x2 - x1 - 1, ' ', ATTR(COL_FG, COL_BG));

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

    vputs(y2 - 1, x1 + 2, "Press any key...", ATTR(COL_FG, COL_BG));
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

        draw_shadow(x1, y1, x2, y2);
        box(x1, y1, x2, y2, "Port Settings");
        for (r = y1 + 1; r < y2; r++)
            vfill(r, x1 + 1, x2 - x1 - 1, ' ', ATTR(COL_FG, COL_BG));

        sprintf(buf, "COM Port:   COM%d", *com_port + 1);
        vputs(y1 + 1, x1 + 2, buf, ATTR(COL_FG, COL_BG));
        vputs(y1 + 2, x1 + 2, "            (Left/Right to change)",
              ATTR(COL_FG, COL_BG));

        sprintf(buf, "Baud Rate:  %s", BAUD_LABELS[*baud_index]);
        vputs(y1 + 4, x1 + 2, buf, ATTR(COL_FG, COL_BG));
        vputs(y1 + 5, x1 + 2, "            (Up/Down to change)",
              ATTR(COL_FG, COL_BG));

        vputs(y2 - 1, x1 + 2, "Press ENTER to connect", ATTR(COL_FG, COL_BG));

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
