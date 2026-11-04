/* SPDX-License-Identifier: GPL-2.0-or-later */
/* save-check: a probe for core-ar2 0.10's ev_project_save.
 *
 * It keeps one project field, sv_saves, and adds 1 to it in every call
 * before a save, so the saved copy carries the count of saves made before
 * it. Along the bottom edge of the screen (y counts from the bottom row),
 * from the left, each row two pixels high:
 *
 *   y 0..1   the calls before a save so far (mod 32, two pixels each)
 *   y 4..5   the calls after a save so far, the same
 *   y 8..9   sv_saves as the project now holds it (mod 32, two pixels each)
 *   y 12..13 the last call's how: its kind (1..3 dots), a gap, the slot's 8
 *            bits (most significant first, a dot for a 1, an empty place for
 *            a 0, 3 pixels apart), a gap, a dot when it failed, and a dot at
 *            x 120 when it was the call before
 *
 * So after SAVE PROJECT AS to another slot the before and after bars are one
 * step longer and sv_saves too; loading the slot that was saved from shows
 * the count it was saved with, which is one less.
 */
#include "analog-rytm-mk2/store.h"

typedef void (*fillrect_t)(void *bmp, int x0, int y0, int x1, int y1, int colour);
#define FILLRECT ((fillrect_t)FILLRECT_AT)

STORE_FIELD(sv_saves);

static volatile unsigned sv_pre, sv_post, sv_how, sv_last_pre, sv_dirty;

void sv_save(unsigned pre, unsigned how)
{
    if (pre) {
        sv_pre++;
        core_store_set(STORE(sv_saves), 0, 0, core_store_get(STORE(sv_saves), 0, 0) + 1);
    } else
        sv_post++;
    sv_how = how;
    sv_last_pre = pre;
    sv_dirty = 1;
}

void sv_project(int how, unsigned dropped)
{
    (void)how;
    (void)dropped;
    sv_dirty = 1;
}

void sv_tick(void *ctrl)
{
    if (sv_dirty) {
        sv_dirty = 0;
        *((volatile unsigned char *)ctrl + 0x20) = 1;  /* redraw */
    }
}

static void bar(void *bmp, int y, unsigned n)
{
    n &= 31;
    if (n)
        FILLRECT(bmp, 0, y, 2 * n - 1, y + 1, 1);
}

void sv_draw(void *bmp, void *ctrl)
{
    unsigned how = sv_how, kind = CORE_SAVE_KIND(how), slot = CORE_SAVE_SLOT(how), i;
    (void)ctrl;
    FILLRECT(bmp, 0, 0, 127, 13, 0);
    bar(bmp, 0, sv_pre);
    bar(bmp, 4, sv_post);
    bar(bmp, 8, core_store_get(STORE(sv_saves), 0, 0));
    for (i = 0; i < kind && i < 3; i++)
        FILLRECT(bmp, 3 * i, 12, 3 * i + 1, 13, 1);
    for (i = 0; i < 8; i++)
        if (slot >> (7 - i) & 1)
            FILLRECT(bmp, 15 + 3 * i, 12, 16 + 3 * i, 13, 1);
    if (how & CORE_SAVE_FAILED)
        FILLRECT(bmp, 45, 12, 46, 13, 1);
    if (sv_last_pre && sv_pre)
        FILLRECT(bmp, 120, 12, 121, 13, 1);
}
