/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe of core-ot 0.31's PATTERN SETTINGS rows (tests/test_octatrack.py and ot_emu).
 * psp[0] edits, [1] the last delta, [2] the value, [3] texts, [4] the track the last call saw. */
#include "octatrack/core.h"

volatile uint32_t psp[6];
const char ps_label[] = "PROBE";
static char ps_buf[4];

void ps_edit(int32_t delta)
{
    psp[0]++; psp[1] = (uint32_t)delta; psp[4] = (uint32_t)ot_pset_track();
    psp[2] = delta ? (psp[2] + (uint32_t)delta) & 7 : 0;
}

const char *ps_text(void)
{
    psp[3]++; psp[4] = (uint32_t)ot_pset_track();
    ps_buf[0] = 'V'; ps_buf[1] = (char)('0' + (psp[2] & 7)); ps_buf[2] = 0;
    return ps_buf;
}
