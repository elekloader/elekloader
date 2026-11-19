/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe of core-ot 0.39's ev_dsp_feed1: 32 16-bit words (0x5a00 + the index) to core 1's $6320 a frame,
 * written through the uncached alias (the eDMA reads RAM). d1p[0] handler calls, [1] transfers started,
 * [2] ot_dsp1_fed. */
#include "octatrack/core.h"

volatile uint32_t d1p[3];
static uint16_t block[32] __attribute__((aligned(16)));

void d1_feed(void)
{
    volatile uint16_t *w = (volatile uint16_t *)((uintptr_t)block + 0x08000000u);
    d1p[0]++;
    for (unsigned i = 0; i < 32; i++)
        w[i] = (uint16_t)(0x5a00u + i);
    if (ot_dsp_feed1(block, 32, 0x6320))
        d1p[1]++;
    d1p[2] = ot_dsp1_fed;
}
