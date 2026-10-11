/* SPDX-License-Identifier: GPL-2.0-or-later */
/* hooks-check: exercises every event core-ar2 provides on the Analog Rytm
 * MKII (OS 1.73), changing nothing the unit does: it takes no key or
 * encoder event, and only draws over the frame.
 *
 * Along the top of the screen, each a 2-pixel bar from the left edge whose
 * length (mod 128) is a count:
 *   y 60-61  key events (ev_key: press, repeat and release each count)
 *   y 56-57  encoder events (ev_enc)
 *   y 52-53  HOOK TEST selected in SETTINGS (ev_settings, through the row's
 *            select callback)
 *   y 48-49  audio blocks / 64 (ev_render_in, 1500 a second: the bar
 *            crawls across in about 5.5 s and wraps)
 * and in the top-right corner a 3x3 square that blinks about once a second
 * (ev_tick: it also asks for a redraw every 8 ticks, so the bars move with
 * nothing else on screen changing). If ev_render_in and ev_render_out ever
 * differ by more than one block, a second square appears below it.
 */
typedef void (*fillrect_t)(void *bmp, int x0, int y0, int x1, int y1, int colour);
#ifndef FILLRECT_AT
#error "FILLRECT_AT: fill_rect's address (mod.json cflags)"
#endif
#define FILLRECT ((fillrect_t)FILLRECT_AT)

extern void core_additem(void *menu, const void *row);
extern void hc_label(void);                /* label.s: the row's name */

static volatile unsigned hc_keys, hc_encs, hc_ticks, hc_selects;
static volatile unsigned hc_rin, hc_rout;

void hc_tick(void *ctrl)
{
    if ((++hc_ticks & 7) == 0)
        *((volatile unsigned char *)ctrl + 0x20) = 1;
}

int hc_key(void *brain, void *ev)
{
    (void)brain; (void)ev;
    hc_keys++;
    return 0;
}

int hc_enc(void *brain, void *ev)
{
    (void)brain; (void)ev;
    hc_encs++;
    return 0;
}

void hc_render_in(void)
{
    hc_rin++;
}

void hc_render_out(void)
{
    hc_rout++;
}

static void hc_select(void)
{
    hc_selects++;
}

static int hc_nothing(void)
{
    return 0;
}

static const void *const hc_row[4] = {
    (const void *)hc_label, (const void *)hc_select,
    (const void *)hc_nothing, (const void *)hc_nothing,
};

void hc_settings(void *menu)
{
    core_additem(menu, hc_row);
}

static void hc_bar(void *bmp, int y, unsigned n)
{
    int len = n & 127;
    FILLRECT(bmp, 0, y, 127, y + 1, 0);
    if (len)
        FILLRECT(bmp, 0, y, len - 1, y + 1, 1);
}

void hc_draw(void *bmp, void *ctrl)
{
    unsigned rin = hc_rin, rout = hc_rout, d = rin - rout;
    (void)ctrl;
    hc_bar(bmp, 60, hc_keys);
    hc_bar(bmp, 56, hc_encs);
    hc_bar(bmp, 52, hc_selects);
    hc_bar(bmp, 48, rin >> 6);
    FILLRECT(bmp, 125, 61, 127, 63, (hc_ticks >> 4) & 1);
    if (d > 1 && d != 0xffffffffu)
        FILLRECT(bmp, 125, 56, 127, 58, 1);
}
