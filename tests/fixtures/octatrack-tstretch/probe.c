/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe of core-ot 0.33's ev_tstretch (tests/test_octatrack.py and ot_emu).
 * tsp[0..7] passes per track, [8] nonzero: take (skip) every track. */
#include "octatrack/core.h"

volatile uint32_t tsp[9];

int ts_track(uint32_t trk)
{
    if (trk < 8)
        tsp[trk]++;
    return tsp[8] != 0;
}
