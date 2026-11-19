/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe of core-ot 0.42's events (tests/test_octatrack.py and ot_emu). u42[0] the control word
 * (bit 0: ev_track_select refuses, bit 1: ev_track_play refuses, bit 2: ev_pattern_select refuses),
 * u42[1] entries logged, then the last 24 in a ring (entry n at 2 + n % 24): event code << 24 | a << 12 | b (12 bits each); windows
 * are counted instead: u42[26] creates, [27] destroys, [28] the last create's level << 16 | its opener's
 * return address & 0xffff, [29] the last destroy's. */
#include "octatrack/core.h"

volatile uint32_t u42[32];

static void log1(unsigned code, unsigned a, unsigned b)
{
    u42[2 + u42[1] % 24] = code << 24 | (a & 0xfff) << 12 | (b & 0xfff);
    u42[1]++;
}

void u_page(unsigned old, unsigned nw) { log1(0x01, old, nw); }
int u_track(unsigned old, unsigned nw, unsigned how) { log1(0x02 | how << 4, old, nw); return u42[0] & 1; }
void u_window(unsigned op, void *w, unsigned a, unsigned ra)
{
    (void)w;
    u42[26 + op]++;
    u42[28 + op] = (op ? 0 : a << 16) | (ra & 0xffff);
}
int u_xfade(unsigned v, unsigned src) { log1(0x04, v, src); return -1; }
uint32_t u_tempo(uint32_t t, unsigned src, unsigned ra) { (void)ra; log1(0x05, t, src); return 0; }
int u_psel(unsigned bank, unsigned pat, unsigned ra, unsigned queued)
{
    log1(0x06 | queued << 4, bank, pat); (void)ra;
    return u42[0] >> 2 & 1;
}
void u_pedit(int trk, unsigned bank, unsigned pat, unsigned load) { log1(0x07 | load << 4, (unsigned)trk, bank << 6 | pat); }
int u_arrmode(unsigned on) { log1(0x08, on, 0); return 0; }
void u_arrrow(unsigned row, unsigned how) { log1(0x09, row, how); }
int u_play(unsigned trk, int slot, unsigned flags, unsigned mode)
{
    log1(0x0a, trk << 4 | mode, (unsigned)slot); (void)flags;
    return u42[0] >> 1 & 1;
}
int u_scene(unsigned enc, int delta, unsigned scene) { log1(0x0b | scene << 4, enc, (unsigned)delta); return 0; }
