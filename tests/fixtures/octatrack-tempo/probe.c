/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe of core-ot 0.41's ev_tempo (tests/test_octatrack.py and ot_emu).
 * tpp[0] commits, [1] the last tempo, [2] its source (1 pattern, 0 project), [3] the control word:
 * nonzero = the tempo to commit instead. */
#include "octatrack/core.h"

volatile uint32_t tpp[4];

uint32_t tp_tempo(uint32_t tempo, unsigned source)
{
    tpp[0]++;
    tpp[1] = tempo;
    tpp[2] = source;
    return tpp[3];
}
