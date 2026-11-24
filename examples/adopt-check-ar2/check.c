/* SPDX-License-Identifier: GPL-2.0-or-later */
/* adopt-check: a probe for core-ar2 0.20's ev_store_adopt.
 *
 * Core calls ad_adopt for each sound and kit that loads with a layout ID or
 * generation this build does not read. The probe counts the calls, by
 * scope and by whether the stored item carries an ID, and stands in for a
 * mod whose older build kept its data in the stored item's first spare
 * bytes: a nonzero stored sound +0x70..+0x72 becomes its sound field
 * ad_sound, and a nonzero stored kit +0x816..+0x819 its kit field ad_kit.
 * Those are where any layout keeps its first 24 sound bits and its first 32
 * kit bits, so a project saved by store-check (whose sc_sound and sc_kit_a
 * sit there) shows them here. Along the bottom edge of the screen (y counts
 * from the bottom row), from the left, each row two pixels high:
 *
 *   y 0..1   sounds handed over with no ID (mod 32, two pixels each)
 *   y 4..5   sounds handed over with another layout's ID, the same
 *   y 8..9   kits handed over with no ID, the same
 *   y 12..13 kits handed over with another layout's ID, the same
 *   y 16..17 a dot at x 0 when the current sound's ad_sound is not 0, and
 *            one at x 4 when the current kit's ad_kit is not 0
 *
 * After the first load the adopted items carry this build's ID once they
 * are saved, so a second load of the saved project adds nothing to the
 * bars.
 */
#include "analog-rytm-mk2/store.h"
#include "analog-rytm-mk2/moments.h"

typedef void (*fillrect_t)(void *bmp, int x0, int y0, int x1, int y1, int colour);
#define FILLRECT ((fillrect_t)FILLRECT_AT)

#define STORED_SOUND_FIRST 0x70        /* the stored sound's first spare byte */
#define STORED_KIT_FIRST 0x816         /* the stored kit's first spare byte */

STORE_FIELD(ad_sound);
STORE_FIELD(ad_kit);

static volatile unsigned ad_count[2][2], ad_dirty;

static unsigned be(const unsigned char *p, int n)
{
    unsigned v = 0;
    while (n--)
        v = v << 8 | *p++;
    return v;
}

void ad_adopt(unsigned scope, void *live, const unsigned char *src, unsigned id)
{
    unsigned v;
    if (scope == STORE_SOUND) {
        ad_count[0][id != 0]++;
        if ((v = be(src + STORED_SOUND_FIRST, 3)) != 0)
            core_store_set(STORE(ad_sound), live, 0, v);
    } else if (scope == STORE_KIT) {
        ad_count[1][id != 0]++;
        if ((v = be(src + STORED_KIT_FIRST, 4)) != 0)
            core_store_set(STORE(ad_kit), live, 0, v);
    }
    ad_dirty = 1;
}

void ad_project(int how, unsigned dropped)
{
    (void)how;
    (void)dropped;
    ad_dirty = 1;
}

void ad_tick(void *ctrl)
{
    if (ad_dirty) {
        ad_dirty = 0;
        *((volatile unsigned char *)ctrl + 0x20) = 1;  /* redraw */
    }
}

static void bar(void *bmp, int y, unsigned n)
{
    n &= 31;
    if (n)
        FILLRECT(bmp, 0, y, 2 * n - 1, y + 1, 1);
}

void ad_draw(void *bmp, void *ctrl)
{
    void *sound = core_current_sound_item(), *kit = core_current_kit_item();
    (void)ctrl;
    FILLRECT(bmp, 0, 0, 127, 17, 0);
    bar(bmp, 0, ad_count[0][0]);
    bar(bmp, 4, ad_count[0][1]);
    bar(bmp, 8, ad_count[1][0]);
    bar(bmp, 12, ad_count[1][1]);
    if (sound && core_store_get(STORE(ad_sound), sound, 0))
        FILLRECT(bmp, 0, 16, 1, 17, 1);
    if (kit && core_store_get(STORE(ad_kit), kit, 0))
        FILLRECT(bmp, 4, 16, 5, 17, 1);
}
