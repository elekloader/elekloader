/* SPDX-License-Identifier: GPL-2.0-or-later */
/* pages-check: a test for core-a4's parameter routines on the Analog
 * Four MKII (OS 1.56), from 0.2 for core-a4 0.3. It contributes one core_params entry, for AMP PAN
 * (parameter 97), and changes nothing else:
 *   - text: the value is shown as two hex digits (its whole part, 8.8),
 *     "00" to "7F", where stock shows its own text;
 *   - delta: counts each turn and passes it on, so the knob still turns;
 *   - get: passes.
 * and a page of its own, CHECK (page 48), on a synth track behind the AMP
 * key after stock's AMP page, through core_page_fns (core-a4 0.3) and
 * core_view_pages. Its knob A is AMP PAN again; its knob B is AMP PAN too
 * after an odd number of PAN's steps and empty after an even number, so
 * each step shows or hides it at the next redraw (page functions); the rest
 * are empty. The AMP key steps over it until PAN has been turned once.
 * Each step PAN turns (a value step, not an encoder count) also lengthens a
 * 2-pixel bar along the bottom of the screen (mod 128), so a turn that
 * reaches core shows even where stock draws no text. (The top rows are the
 * page's inverted header.)
 */
#include <stdint.h>
#include "analog-four-mk2/pages.h"

typedef void (*fillrect_t)(void *bmp, int x0, int y0, int x1, int y1, int colour);
#ifndef FILLRECT_AT
#error "FILLRECT_AT: fill_rect's address (mod.json cflags)"
#endif
#define FILLRECT ((fillrect_t)FILLRECT_AT)

#define PAN 97

static volatile unsigned pc_turns;

static int pc_delta(void *view, int32_t id, int32_t delta, int32_t func)
{
    (void)view; (void)id; (void)delta; (void)func;
    pc_turns++;
    return 0;
}

static int pc_text(struct core_text *t)
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned v = ((unsigned)t->value >> 8) & 0xff;
    t->buf[0] = hex[v >> 4];
    t->buf[1] = hex[v & 15];
    t->buf[2] = 0;
    return CORE_TEXT_DONE;
}

const struct core_param pc_params = {PAN, PAN, 0, pc_delta, pc_text};

static const struct core_page pc_page = {48, "CHECK", {PAN}};
static const struct core_page pc_page_b = {48, "CHECK", {PAN, PAN}};

static const struct core_page *pc_info(int32_t page)
{
    (void)page;
    return (pc_turns & 1) ? &pc_page_b : &pc_page;
}

const struct core_page_fn pc_page_fn = {48, pc_info};

static int32_t pc_shown(void *view)
{
    (void)view;
    return pc_turns != 0;
}

const struct core_view_page pc_view = {CORE_TRACK_SYNTH, CORE_KEY_AMP, 48, pc_shown};

void pc_draw(void *bmp, void *ctrl)
{
    unsigned n = pc_turns & 127;
    (void)ctrl;
    FILLRECT(bmp, 0, 0, 127, 1, 0);
    if (n)
        FILLRECT(bmp, 0, 0, (int)n - 1, 1, 1);
}
