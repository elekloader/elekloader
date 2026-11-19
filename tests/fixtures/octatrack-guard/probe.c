/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe of core-ot 0.36 (tests/test_octatrack.py and ot_emu). gpp[0] pattern checks, [1] the last
 * pattern, [2] choices, [3] the last slot, [4] its track, [5] its effect id, [6] nonzero: refuse. */
#include "octatrack/core.h"

volatile uint32_t gpp[7];

void gp_pattern(void *pattern)
{
    gpp[0]++; gpp[1] = (uint32_t)pattern;
}

int gp_choose(uint32_t slot, uint32_t trk, uint32_t id)
{
    gpp[2]++; gpp[3] = slot; gpp[4] = trk; gpp[5] = id;
    return gpp[6] != 0;
}
