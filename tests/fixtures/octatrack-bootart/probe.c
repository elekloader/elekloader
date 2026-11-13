/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe of core-ot 0.37's ev_boot_frame. bap[0] frames, [1] the last frame, [2] the plane. It takes every
 * frame and draws a bar across the plane's top, so stock's logo never shows. */
#include "octatrack/core.h"

volatile uint32_t bap[3];
static const uint32_t bar_bits[8] = {0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u,
                                     0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u};
static const struct ot_bitmap bar = {8, 1, 1, bar_bits, bar_bits};

int ba_frame(uint32_t frame, void *plane)
{
    bap[0]++; bap[1] = frame; bap[2] = (uint32_t)plane;
    ot_blit(&bar, plane, 0, 0);
    return 1;
}
