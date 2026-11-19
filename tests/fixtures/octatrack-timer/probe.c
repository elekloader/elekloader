/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe of ot_timer_start and ot_vector_get (tests/test_octatrack.py and ot_emu).
 * tmp[0] the slot, [1] runs, [2] handler 0x41 (the frame interrupt) and [3] handler 0x5b, read by the timer. */
#include "octatrack/core.h"

volatile uint32_t tmp[4];

static void tm_tick(void)
{
    tmp[1]++;
    tmp[2] = (uint32_t)ot_vector_get(0x41);
    tmp[3] = (uint32_t)ot_vector_get(0x5b);
}

void tm_boot(void)
{
    tmp[0] = (uint32_t)ot_timer_start(6, tm_tick);
}
