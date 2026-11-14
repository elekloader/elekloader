/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe of core-ot 0.41's ev_preview and ev_rec_cmd (tests/test_octatrack.py and ot_emu).
 * pvp[0] entries logged, then up to 8 entries: 0x5TOF00SS for ev_preview (track, on, flex, slot) and
 * 0x7T00CCCC | quant << 16 for ev_rec_cmd (track, cmd). */
#include "octatrack/core.h"

volatile uint32_t pvp[9];

static void log1(uint32_t w)
{
    if (pvp[0] < 8)
        pvp[1 + pvp[0]] = w;
    pvp[0]++;
}

void pv_preview(unsigned trk, unsigned on, unsigned flex, unsigned slot)
{
    log1(0x50000000u | (trk & 15) << 24 | (on & 15) << 20 | (flex & 15) << 16 | (slot & 0xff));
}

void pv_rec(unsigned trk, unsigned cmd, unsigned quant)
{
    log1(0x70000000u | (trk & 15) << 24 | (quant & 0xff) << 16 | (cmd & 0xffff));
}
