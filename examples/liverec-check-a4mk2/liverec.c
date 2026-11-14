/* SPDX-License-Identifier: GPL-2.0-or-later */
/* liverec-check: core-a4mk2's ev_live_rec and core_mutes (analog-four-mk2/ui.h,
 * moments.h), recorded in liverec_check (an exported symbol) for an emulator's or a
 * unit's memory read. Big-endian:
 *   magic       0x4c524134 ("LRA4") once an event ran
 *   calls[t][w] ev_live_rec's calls on track t (0..5) of kind w (1 note, 2 value,
 *               3 lock; [t][0] counts kinds out of range)
 *   taken[t][w] the calls this mod took: every call on TAKE_TRACK (track 2, index 1)
 *   mutes       core_mutes() at the last tick; mutes_changes how often it changed
 *   ticks       ev_tick's calls
 * Live recording on track 2 therefore writes nothing; tracks 1, 3..6 record as stock. */
#include <stdint.h>
#include "analog-four-mk2/ui.h"
#include "analog-four-mk2/moments.h"

#define TRACKS 6
#define TAKE_TRACK 1

struct liverec_check {
    uint32_t magic;
    uint32_t calls[TRACKS][4], taken[TRACKS][4];
    uint32_t mutes, mutes_changes, ticks;
} liverec_check;

int lr_live(unsigned track, unsigned what)
{
    liverec_check.magic = 0x4c524134;
    if (track >= TRACKS)
        return 0;
    if (what > 3)
        what = 0;
    liverec_check.calls[track][what]++;
    if (track == TAKE_TRACK) {
        liverec_check.taken[track][what]++;
        return 1;
    }
    return 0;
}

void lr_tick(void)
{
    uint32_t m = core_mutes();
    liverec_check.magic = 0x4c524134;
    liverec_check.ticks++;
    if (m != liverec_check.mutes) {
        liverec_check.mutes = m;
        liverec_check.mutes_changes++;
    }
}
