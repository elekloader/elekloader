/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe of core-ot 0.28's LFO waves (tests/test_octatrack.py and ot_emu): two waves, and what they saw.
 * lwp[0] PLS calls, [1] its last record, [2] its last phase, [3] its last restart, [4] restarts seen,
 * [5] HALF calls, [6] its last record, [7] a label buffer for a direct call of the WAVE formatter. */
#include "octatrack/core.h"

volatile uint32_t lwp[12];

static void pls(struct ot_lfo *l, int32_t restart)
{
    lwp[0]++; lwp[1] = (uint32_t)l; lwp[2] = l->phase; lwp[3] = (uint32_t)restart;
    if (restart) lwp[4]++;
    l->out = l->phase < 0x200000u ? 0x7fffff : -0x800000;
}

static void half(struct ot_lfo *l, int32_t restart)
{
    (void)restart;
    lwp[5]++; lwp[6] = (uint32_t)l;
    l->out = 0x400000;
}

const struct ot_lfo_wave lw_pls = {"PLS", pls};
const struct ot_lfo_wave lw_half = {"HALF", half};
