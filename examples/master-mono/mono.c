/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Master mono: a page of the master pages (FUNC+LFO), after the internal mixer, whose knob A sets what the
 * outputs get: STEREO (as stock), MONO (left and right summed) or SWAP (left and right swapped). On every
 * output: main, headphones and USB. A template for core 4.1's master output (ev_render_master) and master
 * pages (core_mpages): it names no firmware address, so its 1.54 port is empty. */
#include "digitakt-mk1/core3.h"

enum { STEREO, MONO, SWAP, MODES };
static const char *const NAME[MODES] = {"STEREO", "MONO", "SWAP"};
static volatile int32_t mode;           /* the UI task writes it, the render reads it */
static int32_t turned;                  /* knob A's counts since its last step */

/* the render, at interrupt level: 32 frames of {L, R} */
void mono_render(int32_t *pair)
{
    int32_t m = mode, i, l, r;
    if (m == STEREO)
        return;
    for (i = 0; i < 64; i += 2) {
        l = pair[i];
        r = pair[i + 1];
        if (m == MONO)
            pair[i] = pair[i + 1] = (l >> 1) + (r >> 1);
        else {
            pair[i] = r;
            pair[i + 1] = l;
        }
    }
}

/* the page, below the title bar the stock draw has drawn (rows 53-63); y 0 is the bottom row. The stock draw
 * leaves kind 0's empty knob boxes there: clear them first. */
static void mono_draw(void *view, void *bmp)
{
    int32_t k;
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    for (k = 0; k < MODES; k++) {
        int32_t x = 8 + 40 * k;
        fw_textf(bmp, fw_font5, x + 2, 26, -1, "%s", NAME[k]);
        if (k == mode)
            fw_fillrect(bmp, x, 24, x + 36, 33, -1);    /* -1 inverts */
    }
    fw_textf(bmp, fw_font5, 2, 4, -1, "A: OUTPUT");
}

/* the view's knobs: knob A (1), 16 counts a notch, one mode a notch */
static int32_t mono_enc(void *view, void *event)
{
    int32_t knob = *(int32_t *)((char *)event + 12), turn = *(int32_t *)((char *)event + 16);
    if (knob != 1)
        return 0;                           /* the others: the stock handler (nothing on this page) */
    turned += turn;
    if (turned >= 16 || turned <= -16) {
        int32_t m = mode + (turned > 0 ? 1 : -1);
        mode = m < 0 ? 0 : m >= MODES ? MODES - 1 : m;
        turned = 0;
        fw_invalidate(view);
    }
    return 1;
}

/* FUNC + held LFO + PLAY (CLEAR) on the page: back to STEREO */
static void mono_clear(void *view)
{
    mode = STEREO;
    turned = 0;
    fw_invalidate(view);
}

struct core_mpage mono_page = {12, "MONO", "Master mono", mono_draw, 0, mono_enc, 0, 0, mono_clear};
