/* SPDX-License-Identifier: GPL-2.0-or-later */
/* hooks-check-dn2: exercises core-dn2's six UI events on the Digitone
 * II (OS 1.12). It changes nothing the unit does until its PERSONALIZE row
 * is selected; from then on it takes [TEMPO] (key code SWALLOW_CODE), so
 * the TEMPO page no longer opens and the stock handler never sees the key.
 *
 * Along the top of the screen, each a 2-pixel bar from the left edge whose
 * length (mod 128) is a count:
 *   y 60-61  key events reaching hc_key (ev_key, order 60)
 *   y 56-57  encoder events (ev_enc)
 *   y 52-53  HOOK TEST selected in SETTINGS (ev_settings)
 *   y 48-49  HOOK TEST selected in PERSONALIZE (ev_personalize)
 *   y 44-45  key events hc_first took (ev_key, order 40)
 * and in the top-right corner a 3x3 square that blinks about once a second
 * (ev_tick: it also asks for a redraw every 8 ticks, so the bars move with
 * nothing else on screen changing). A second square below it appears if
 * hc_key ever saw an event before hc_first did: the order is broken.
 */
typedef void (*fillrect_t)(void *bmp, int x0, int y0, int x1, int y1, int colour);
#ifndef FILLRECT_AT
#error "FILLRECT_AT: Bitmap::fillRect's address (mod.json cflags)"
#endif
#define FILLRECT ((fillrect_t)FILLRECT_AT)
#ifndef SWALLOW_CODE
#error "SWALLOW_CODE: the key code hc_first takes once armed (mod.json cflags)"
#endif

extern void core_additem(void *menu, const void *row);
extern void hc_label(void);                /* label.s: the row's name */

/* not static, so a debugger or an emulator can read them from the link map */
volatile unsigned hc_keys, hc_encs, hc_ticks, hc_selects, hc_pselects, hc_taken;
volatile unsigned hc_armed, hc_order_bad, hc_last_code, hc_last_action;
volatile void *hc_last_ev;

void hc_tick(void *ctrl)
{
    if ((++hc_ticks & 7) == 0)
        *((volatile unsigned char *)ctrl + 0x20) = 1;
}

/* KeyEvent (0x40116278): +12 the message's code, +16 its action */
int hc_first(void *brain, void *ev)
{
    (void)brain;
    hc_last_ev = ev;
    hc_last_code = *(unsigned *)((char *)ev + 12);
    hc_last_action = *(unsigned *)((char *)ev + 16);
    if (hc_armed && hc_last_code == SWALLOW_CODE) {
        hc_taken++;
        return 1;
    }
    return 0;
}

int hc_key(void *brain, void *ev)
{
    (void)brain;
    if (ev != hc_last_ev)
        hc_order_bad = 1;
    hc_last_ev = 0;
    hc_keys++;
    return 0;
}

int hc_enc(void *brain, void *ev)
{
    (void)brain; (void)ev;
    hc_encs++;
    return 0;
}

static void hc_select(void)
{
    hc_selects++;
}

static void hc_pselect(void)
{
    hc_pselects++;
    hc_armed = 1;
}

static int hc_nothing(void)
{
    return 0;
}

static const void *const hc_row[4] = {
    (const void *)hc_label, (const void *)hc_select,
    (const void *)hc_nothing, (const void *)hc_nothing,
};

static const void *const hc_prow[4] = {
    (const void *)hc_label, (const void *)hc_pselect,
    (const void *)hc_nothing, (const void *)hc_nothing,
};

void hc_settings(void *menu)
{
    core_additem(menu, hc_row);
}

void hc_personalize(void *menu)
{
    core_additem(menu, hc_prow);
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
    (void)ctrl;
    hc_bar(bmp, 60, hc_keys);
    hc_bar(bmp, 56, hc_encs);
    hc_bar(bmp, 52, hc_selects);
    hc_bar(bmp, 48, hc_pselects);
    hc_bar(bmp, 44, hc_taken);
    FILLRECT(bmp, 125, 61, 127, 63, (hc_ticks >> 4) & 1);
    if (hc_order_bad)
        FILLRECT(bmp, 125, 56, 127, 58, 1);
}
