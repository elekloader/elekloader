/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-hello-user: a +Drive mod that uses another one's export.
 *
 * It imports drive_hello_ticks from drive-hello (and requires it, so the
 * loader refuses it alone), and after the firmware draws each frame it draws
 * a bar along the bottom row whose length follows the count: 0 to 127
 * pixels, a step every 8 ticks. Its ev_draw entry's order (5) is lower than
 * the flashed mods' entries, so the merged table calls it first. */
typedef void (*fillrect_t)(void *bmp, int x0, int y0, int x1, int y1, int colour);

#ifndef FILLRECT_AT
#error "FILLRECT_AT: Bitmap::fillRect's address in the target OS (mod.json cflags)"
#endif
#define FILLRECT ((fillrect_t)FILLRECT_AT)

extern unsigned drive_hello_ticks;

__attribute__((used)) static void drive_hello_draw(void *bmp, void *ctrl)
{
    (void)ctrl;
    FILLRECT(bmp, 0, 0, (int)((drive_hello_ticks >> 3) & 127), 0, 1);
}
