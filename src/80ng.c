#include <conio.h>
#include <c128/vdc.h>
#include "hw.h"
#include "sound.h"
#include "input.h"

#define COLS 80
#define ROWS 25
#define CELLS 2000
#define ATTR 0x0800
#define FONT 0x2000
#define LEFT_X 24
#define RIGHT_X 608
// Deliberately short bars: they cannot cover the full court height.
#define BAR_HEIGHT 32
// VDC pixels are about twice as tall as they are wide, so an aspect-correct
// ball is roughly twice as wide as it is high. Keeping BALL_W == 2*BALL_H
// makes the blob read as a circle instead of a vertical rectangle.
#define BALL_H 4
#define BALL_W 8
// Per-row horizontal extent of the ball (pixels): {width, left inset}. Row 0
// is the top; the values widen toward the middle so the blob is a rounded
// disc rather than a box. Insets are measured from ball.x.
static const char ball_row[6][2] = {
    {4, 2}, // top rows:    4px wide, indented 2
    {8, 0}, // upper rows:  full width
    {8, 0}, // lower rows:  full width
    {4, 2}, // bottom rows: 4px wide, indented 2
    {0, 0}, // (spare)
    {0, 0}, // (spare)
};
#define BALL_DX_BASE 3
#define BALL_DX_MAX 8
#define TOP 8
#define BOTTOM 192
#define DIGITS 21
#define BALL_GLYPH 35
#define BAR_RAIL_GLYPH 39
#define GLYPHS 47
#define DIRTY_MAX 48

// VDC colour plane: attribute byte is (background<<4)|foreground.
// Colours are the standard C128 16-colour palette.
#define C_BLACK 0x0
#define C_DKGREY 0x1
#define C_DKBLUE 0x2
#define C_LTBLUE 0x3
#define C_DKGREEN 0x4
#define C_LTGREEN 0x5
#define C_DKCYAN 0x6
#define C_LTCYAN 0x7
#define C_DKRED 0x8
#define C_LTRED 0x9
#define C_DKPURPLE 0xA
#define C_LTPURPLE 0xB
#define C_DKYELLOW 0xC
#define C_LTYELLOW 0xD
#define C_YELLOW C_LTYELLOW
#define C_LTGREY 0xE
#define C_WHITE 0xF
#define ATTR_BYTE(bg, fg) ((char)(((bg) << 4) | (fg)))

// Colours for the fixed background (glyph 0) and playfield glyphs.
// These are chosen so the floor/net stay dim and the objects pop.
#define COL_BG ATTR_BYTE(C_BLACK, C_BLACK)
#define COL_RAIL_TOP ATTR_BYTE(C_BLACK, C_WHITE)
#define COL_NET ATTR_BYTE(C_BLACK, C_WHITE)

#define COL_BAR_L ATTR_BYTE(C_BLACK, C_LTGREEN)
#define COL_BAR_R ATTR_BYTE(C_BLACK, C_YELLOW)

#define COL_BALL_LO ATTR_BYTE(C_BLACK, C_WHITE)
#define COL_BALL_HI ATTR_BYTE(C_BLACK, C_WHITE)

#define COL_TEXT ATTR_BYTE(C_BLACK, C_WHITE)

struct Bar
{
    int y;
    char points;
};
struct Ball
{
    int x, y;
    signed char dx, dy;
};
struct Bar left, right;
struct Ball ball;
static char screen[CELLS];
static char attr[CELLS];
static unsigned row_addr[ROWS], dirty[DIRTY_MAX];
static char dirty_count, shapes[GLYPHS][8];
static char saved_font[GLYPHS * 16], saved_regs[37];
// Exposed in the VICE monitor for timing checks.
volatile unsigned frame_count, blank_overruns;
volatile char max_dirty;

static const char digits[14][8] = {
    {0x3c, 0x66, 0x6e, 0x76, 0x66, 0x66, 0x3c, 0},
    {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7e, 0},
    {0x3c, 0x66, 0x06, 0x0c, 0x30, 0x60, 0x7e, 0},
    {0x3c, 0x66, 0x06, 0x1c, 0x06, 0x66, 0x3c, 0},
    {0x0c, 0x1c, 0x3c, 0x6c, 0x7e, 0x0c, 0x0c, 0},
    {0x7e, 0x60, 0x7c, 0x06, 0x06, 0x66, 0x3c, 0},
    {0x1c, 0x30, 0x60, 0x7c, 0x66, 0x66, 0x3c, 0},
    {0x7e, 0x06, 0x0c, 0x18, 0x30, 0x30, 0x30, 0},
    {0x3c, 0x66, 0x66, 0x3c, 0x66, 0x66, 0x3c, 0},
    {0x3c, 0x66, 0x66, 0x3e, 0x06, 0x0c, 0x38, 0},
    {0x7c, 0x66, 0x66, 0x7c, 0x60, 0x60, 0x60, 0}, // P
    {0x3c, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3c, 0}, // O
    {0x66, 0x76, 0x7e, 0x7e, 0x6e, 0x66, 0x66, 0}, // N
    {0x3c, 0x66, 0x60, 0x6e, 0x66, 0x66, 0x3c, 0}  // G
};

void wait_frame(void)
{
    // BustedBoulders $1f4b: wait for a NEW VDC blank, not the VIC IRQ.
    while (vdc.addr & 0x20)
    {
    }
    while (!(vdc.addr & 0x20))
    {
    }
}

static char background_lookup(char x, char y)
{
    // Top and bottom court lines.
    if (y == 1 || y == 24)
        return 1;

    // Centre net only inside the court.
    if (x == 40 && y > 1 && y < 24 && !(y & 1))
        return 3;

    return 0;
}

// Attribute that the background at (x,y) renders with. Mirrors the glyph
// choices above so erased cells restore their own colour, not a flat one.
static char background_attr(char x, char y)
{
    if (y == 1 || y == 24)
        return COL_RAIL_TOP;

    if (x == 40 && y > 1 && y < 24 && !(y & 1))
        return COL_NET;

    return COL_BG;
}

static void set_cell(char x, char y, char glyph)
{
    unsigned addr = row_addr[y] + x;
    char i;
    if (screen[addr] == glyph)
        return;
    screen[addr] = glyph;
    // Erase/draw can touch the same cell: commit its final value once.
    for (i = 0; i < dirty_count; ++i)
        if (dirty[i] == addr)
            return;
    // Maximum: 28 bar + 8 ball + 4 points cells, including old/new.
    dirty[dirty_count++] = addr;
}

// Same as set_cell, but also stages the cell's attribute byte so a colour
// change on its own is committed even when the glyph does not change.
static void set_cell_col(char x, char y, char glyph, char attribute)
{
    unsigned addr = row_addr[y] + x;
    char i, changed = 0;
    if (screen[addr] != glyph)
    {
        screen[addr] = glyph;
        changed = 1;
    }
    if (attr[addr] != attribute)
    {
        attr[addr] = attribute;
        changed = 1;
    }
    if (!changed)
        return;
    for (i = 0; i < dirty_count; ++i)
        if (dirty[i] == addr)
            return;
    dirty[dirty_count++] = addr;
}

static void make_shapes(void)
{
    char g, y;
    for (y = 0; y < 8; ++y)
    {
        shapes[1][y] = y == 0 ? 0xff : 0;
        shapes[2][y] = shapes[3][y] = 0x18;
        shapes[4][y] = 0xff;
        for (g = 0; g < 8; ++g)
        {
            shapes[5 + g][y] = y >= g ? 0xff : 0;
            shapes[13 + g][y] = y < g ? 0xff : 0;
            // Same partial bar top, but preserve the top court rail so
            // a bar head overlapping row 1 does not erase it.
            shapes[BAR_RAIL_GLYPH + g][y] =
                (y == 0 || y >= g) ? 0xff : 0;
        }
        for (g = 0; g < 14; ++g)
            shapes[DIGITS + g][y] = digits[g][y];
    }
}

static void bar_cells(char x, int y, char erase, char colour)
{
    // Bar spans BAR_HEIGHT pixels from pixel row y. Cell 0 shows the
    // upward-slanted head (5+phase) when y is not cell aligned; the remaining
    // cells are solid (glyph 4) except a downward tail cell when the bar
    // does not end on a cell boundary (glyph 13+n).
    char row = y >> 3, phase = y & 7;
    char cells = (char)((phase + BAR_HEIGHT + 7) >> 3);
    char i;
    for (i = 0; i < cells; ++i)
    {
        char glyph = 4;
        if (i == 0 && phase)
        {
            // Row 1 carries the top court rail; use the variant that keeps
            // it visible instead of the plain partial-top glyph.
            if (row == 1)
                glyph = BAR_RAIL_GLYPH + phase;
            else
                glyph = 5 + phase;
        }
        if (i == cells - 1)
        {
            char end = phase + BAR_HEIGHT - i * 8;
            if (end < 8)
                glyph = 13 + end;
        }
        set_cell_col(x, row + i, erase ? background_lookup(x, row + i) : glyph,
                     erase ? background_attr(x, row + i) : colour);
    }
}

static void erase_ball(void)
{
    char x0 = ball.x >> 3, y0 = ball.y >> 3;
    char x1 = (ball.x + BALL_W - 1) >> 3, y1 = (ball.y + BALL_H - 1) >> 3;
    char x, y;
    for (y = y0; y <= y1; ++y)
        for (x = x0; x <= x1; ++x)
            set_cell_col(x, y, background_lookup(x, y), background_attr(x, y));
}

static void draw_ball(void)
{
    char x0 = ball.x >> 3, y0 = ball.y >> 3;
    char x1 = (ball.x + BALL_W - 1) >> 3, y1 = (ball.y + BALL_H - 1) >> 3;
    char x, y, line;
    // Alternate the ball's colour with its vertical bounce phase so the
    // sprite reads as a moving highlight without a per-row raster trick.
    char ball_colour = (ball.dy < 0) ? COL_BALL_HI : COL_BALL_LO;
    for (y = y0; y <= y1; ++y)
    {
        for (x = x0; x <= x1; ++x)
        {
            char g = BALL_GLYPH + (y - y0) * 2 + (x - x0);
            char under = screen[row_addr[y] + x];
            for (line = 0; line < 8; ++line)
            {
                // Pixel row of the ball this font line corresponds to.
                int py = (y << 3) + line - ball.y;
                unsigned bits = 0;
                if (py >= 0 && py < BALL_H)
                {
                    // Pixel columns the disc covers on this row.
                    int width = ball_row[py][0];
                    int cx0 = ball.x + ball_row[py][1];
                    int cx1 = cx0 + width;
                    int lo = (x << 3), hi = lo + 8;
                    int a = cx0 > lo ? cx0 : lo;
                    int b = cx1 < hi ? cx1 : hi;
                    if (b > a)
                        bits = (0xffu >> (a - lo)) & ~(0xffu >> (b - lo));
                }
                // Preserve the net and bar edges beneath the ball.
                shapes[g][line] = shapes[under][line] | (char)bits;
            }
            set_cell_col(x, y, g, ball_colour);
        }
    }
}

static void draw_points(void)
{
    set_cell_col(27, 0, DIGITS + left.points / 10, COL_TEXT);
    set_cell_col(28, 0, DIGITS + left.points % 10, COL_TEXT);
    set_cell_col(49, 0, DIGITS + right.points / 10, COL_TEXT);
    set_cell_col(50, 0, DIGITS + right.points % 10, COL_TEXT);
}

// Ball speed grows with the rally, as in the original arcade game: it starts at
// BALL_DX_BASE and gains 1 px/tick at 4, 8, 16 and 24 hits (3->4->5->6->7),
// then 1 more at 32 hits to reach the turbo top speed of BALL_DX_MAX (8).
// Clamped to BALL_DX_MAX. A miss (which calls reset_ball) resets it.
static char rally_hits;

// Return the horizontal speed for the current rally length.
static char speed_for_rally(void)
{
    char speed = BALL_DX_BASE;
    if (rally_hits >= 4)
        ++speed;
    if (rally_hits >= 8)
        ++speed;
    if (rally_hits >= 16)
        ++speed;
    if (rally_hits >= 24)
        ++speed;
    if (rally_hits >= 32)
        ++speed;
    if (speed > BALL_DX_MAX)
        speed = BALL_DX_MAX;
    return speed;
}

static void reset_ball(signed char direction)
{
    rally_hits = 0;
    ball.x = 318;
    ball.y = 98;
    ball.dx = direction < 0 ? -BALL_DX_BASE : BALL_DX_BASE;
    ball.dy = -1;
}

static void move_bar(struct Bar *p, char up, char down)
{
    if (up && !down)
        p->y -= 2;
    if (down && !up)
        p->y += 2;
    if (p->y < TOP)
        p->y = TOP;
    if (p->y > BOTTOM - BAR_HEIGHT)
        p->y = BOTTOM - BAR_HEIGHT;
}

static void update_ball(void)
{
    int old_x = ball.x;
    ball.x += ball.dx;
    ball.y += ball.dy;
    // Original movement: bounce off the top and bottom of the court.
    if (ball.y < TOP)
    {
        ball.y = 2 * TOP - ball.y;
        ball.dy = -ball.dy;
        sound_wall();
    }

    if (ball.y > BOTTOM - BALL_H)
    {
        ball.y = 2 * (BOTTOM - BALL_H) - ball.y;
        ball.dy = -ball.dy;
        sound_wall();
    }
    // Crossings of bar faces: never bounce from behind a bar.
    if (ball.dx < 0 && old_x >= LEFT_X + 8 && ball.x <= LEFT_X + 8 &&
        ball.y + BALL_H > left.y && ball.y < left.y + BAR_HEIGHT)
    {
        ball.x = 2 * (LEFT_X + 8) - ball.x;
        ball.dx = -ball.dx;
        ++rally_hits;
        ball.dx = ball.dx < 0 ? -speed_for_rally() : speed_for_rally();
        sound_bar();
    }
    if (ball.dx > 0 && old_x + BALL_W <= RIGHT_X &&
        ball.x + BALL_W >= RIGHT_X &&
        ball.y + BALL_H > right.y && ball.y < right.y + BAR_HEIGHT)
    {
        ball.x = 2 * (RIGHT_X - BALL_W) - ball.x;
        ball.dx = -ball.dx;
        ++rally_hits;
        ball.dx = ball.dx < 0 ? -speed_for_rally() : speed_for_rally();
        sound_bar();
    }
    if (ball.x <= 8)
    {
        right.points = (right.points + 1) % 100;
        sound_point();
        reset_ball(3);
    }
    else if (ball.x + BALL_W >= 632)
    {
        left.points = (left.points + 1) % 100;
        sound_point();
        reset_ball(-3);
    }
}

void prepare_frame(char held)
{
    dirty_count = 0;
    erase_ball();
    bar_cells(LEFT_X / 8, left.y, 1, COL_BAR_L);
    bar_cells(RIGHT_X / 8, right.y, 1, COL_BAR_R);
    move_bar(&left, held & 1, held & 2);
    move_bar(&right, held & 4, held & 8);
    update_ball();
    bar_cells(LEFT_X / 8, left.y, 0, COL_BAR_L);
    bar_cells(RIGHT_X / 8, right.y, 0, COL_BAR_R);
    draw_points();
    draw_ball();
}

void commit_frame(void)
{
    char g, y, i;
    unsigned addr;
    // 32 font bytes plus a bounded cell list, not a 2,000-byte upload.
    for (g = BALL_GLYPH; g < GLYPHS; ++g)
    {
        vdc_mem_addr(FONT + (unsigned)g * 16);
        for (y = 0; y < 8; ++y)
            vdc_write(shapes[g][y]);
    }
    // Rewrite only the cells that changed, character plane and colour
    // plane. The attribute plane sits at ATTR and mirrors the screen.
    for (i = 0; i < dirty_count; ++i)
    {
        vdc_mem_write_at(dirty[i], screen[dirty[i]]);
        addr = ATTR + dirty[i];
        vdc_mem_write_at(addr, attr[dirty[i]]);
    }
    if (dirty_count > max_dirty)
        max_dirty = dirty_count;
    if (!(vdc.addr & 0x20))
        ++blank_overruns;
    ++frame_count;
}

int main(void)
{
    unsigned i;
    char x, y, g, held;
    char old_vic, old_speed, old_pra, old_ddra, old_ddrb;
    iocharmap(IOCHM_ASCII);
    dispmode80col();
    textcursor(0);
    sound_init();
    clrscr();
    __asm { sei }
    old_vic = HW(0xd011) & 0x7f;
    old_speed = HW(0xd030);
    old_pra = HW(0xdc00);
    old_ddra = HW(0xdc02);
    old_ddrb = HW(0xdc03);
    HW(0xd011) = old_vic & 0x6f;
    HW(0xd030) = 1;
    HW(0xdc02) = 0xff;
    HW(0xdc03) = 0;
    for (i = 0; i < 37; ++i)
        saved_regs[i] = vdc_reg_read((VDCRegister)i);
    vdc_reg_write(VDCR_HSTART, 0x80);
    vdc_mem_addr(FONT);
    for (i = 0; i < sizeof(saved_font); ++i)
        saved_font[i] = vdc_read();
    make_shapes();
    vdc_mem_addr(FONT);
    for (g = 0; g < GLYPHS; ++g)
    {
        for (y = 0; y < 8; ++y)
            vdc_write(shapes[g][y]);
        for (y = 0; y < 8; ++y)
            vdc_write(0);
    }
    // The VDC clock is independent of PAL/NTSC VIC timing. Use standard
    // approximately 60 Hz, non-interlaced 80x25 timing on either machine.
    vdc_reg_write(VDCR_HTOTAL, 0x7e);
    vdc_reg_write(VDCR_HSYNC, 0x66);
    vdc_reg_write(VDCR_SYNCSIZE, 0x49);
    vdc_reg_write(VDCR_VTOTAL, 0x20);
    vdc_reg_write(VDCR_VADJUST, 0);
    vdc_reg_write(VDCR_VSYNC, 0x1d);
    vdc_reg_write(VDCR_LACE, 0);
    vdc_reg_write(VDCR_DISP_ADDRH, 0);
    vdc_reg_write(VDCR_DISP_ADDRL, 0);
    vdc_reg_write(VDCR_ATTR_ADDRH, ATTR >> 8);
    vdc_reg_write(VDCR_ATTR_ADDRL, 0);
    vdc_reg_write(VDCR_CHAR_ADDRH, (saved_regs[28] & 0x1f) | 0x20);
    vdc_reg_write(VDCR_HDISPLAY, COLS);
    vdc_reg_write(VDCR_VDISPLAY, ROWS);
    vdc_reg_write(VDCR_CSIZE, 7);
    vdc_reg_write(VDCR_CHEIGHT, 7);
    vdc_reg_write(VDCR_CWIDTH, 0x78);
    vdc_reg_write(VDCR_CURSOR_START, 0x20);
    vdc_reg_write(VDCR_VSCROLL, 0);
    // Bit 6 of HSCROLL enables the per-cell attribute (colour) plane.
    // Without it the whole screen renders from the global COLOR register.
    vdc_reg_write(VDCR_HSCROLL, 0x47 | 0x40);
    vdc_reg_write(VDCR_ROWINC, 0);
    // With attributes on, COLOR only supplies the shared background; the
    // foreground nibble is ignored. Keep it black-on-black so any cell the
    // attribute pass has not reached yet stays dark rather than flashing.
    vdc_reg_write(VDCR_COLOR, (C_BLACK << 4) | C_BLACK);
    for (y = 0; y < ROWS; ++y)
        row_addr[y] = (unsigned)y * COLS;
    for (i = 0; i < CELLS; ++i)
        screen[i] = attr[i] = 0;
    // Lay down the whole static playfield (rails, net, floor) through the
    // same lookup the erase paths use, so the initial frame matches what the
    // game restores to when objects move away.
    for (y = 0; y < ROWS; ++y)
        for (x = 0; x < COLS; ++x)
        {
            screen[row_addr[y] + x] = background_lookup(x, y);
            attr[row_addr[y] + x] = background_attr(x, y);
        }

    left.y = right.y = 72;
    reset_ball(3);
    bar_cells(LEFT_X / 8, left.y, 0, COL_BAR_L);
    bar_cells(RIGHT_X / 8, right.y, 0, COL_BAR_R);
    draw_points();
    draw_ball();
    vdc_mem_addr(0);
    for (i = 0; i < CELLS; ++i)
        vdc_write(screen[i]);
    vdc_mem_addr(ATTR);
    for (i = 0; i < CELLS; ++i)
        vdc_write(attr[i]);
    wait_frame();
    commit_frame();
    vdc_reg_write(VDCR_HSTART, saved_regs[34]);
    for (;;)
    {
        held = keys();
        if (held & 16)
            break;
        prepare_frame(held);
        sound_tick();
        wait_frame();
        commit_frame();
    }
    vdc_reg_write(VDCR_HSTART, 0x80);
    vdc_mem_addr(FONT);
    for (i = 0; i < sizeof(saved_font); ++i)
        vdc_write(saved_font[i]);
    // Skip read-only registers and block-transfer commands.
    for (i = 0; i < 30; ++i)
        if (i != 16 && i != 17)
            vdc_reg_write((VDCRegister)i, saved_regs[i]);
    vdc_reg_write(VDCR_HSTART, saved_regs[34]);
    HW(0xdc00) = old_pra;
    HW(0xdc02) = old_ddra;
    HW(0xdc03) = old_ddrb;
    HW(0xd030) = old_speed;
    HW(0xd011) = old_vic;
    __asm { cli }
    clrscr();
    gotoxy(35, 10);
    for (x = 0; x < 9; ++x)
        putch("GAME OVER"[x]);
    return 0;
}
