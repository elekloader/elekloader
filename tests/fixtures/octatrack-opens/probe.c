/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe of core-ot 0.35 (tests/test_octatrack.py and ot_emu). opp[0] double taps, [1] the last track,
 * [2] SETUP opens, [3] the last kind, [4] nonzero: take both. */
#include "octatrack/core.h"

volatile uint32_t opp[5];

int op_double(uint32_t trk)
{
    opp[0]++; opp[1] = trk;
    return opp[4] != 0;
}

int op_setup(uint32_t kind)
{
    opp[2]++; opp[3] = kind;
    return opp[4] != 0;
}
