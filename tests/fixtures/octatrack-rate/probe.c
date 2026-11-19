/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe of core-ot 0.30's ev_play_increment (tests/test_octatrack.py and ot_emu).
 * rpp[0..7] calls per track, [8..15] the last increment each saw, [16] nonzero: double track 1's,
 * [17] what it returned for track 1 last. */
#include "octatrack/core.h"

volatile uint32_t rpp[18];

int32_t rp_inc(uint32_t trk, int32_t inc)
{
    rpp[trk]++;
    rpp[8 + trk] = (uint32_t)inc;
    if (trk == 0 && rpp[16]) {
        inc *= 2;
        rpp[17] = (uint32_t)inc;
    }
    return inc;
}
