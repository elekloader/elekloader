/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe of core-ot 0.38's ev_track_start. spp[0] STARTs, [1] the last track, [2] its type, [3] stock's
 * START, [4] nonzero: use ours, [5] our START's calls. */
#include "octatrack/core.h"

volatile uint32_t spp[6];
static int (*stock_start)(uint32_t, uint32_t, uint32_t);

static int my_start(uint32_t trk, uint32_t arg, uint32_t flags)
{
    spp[5]++;
    return stock_start(trk, arg, flags);
}

uint32_t sp_start(uint32_t trk, uint32_t type, uint32_t stock)
{
    spp[0]++; spp[1] = trk; spp[2] = type; spp[3] = stock;
    if (!spp[4] || !stock)
        return 0;
    stock_start = (int (*)(uint32_t, uint32_t, uint32_t))stock;
    return (uint32_t)my_start;
}
