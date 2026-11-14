/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * core-ot 0.30: a track's playback increment (docs/ADAPTING.md, "The playback increment on the
 * Octatrack"; the declarations are in elekloader/sdk/include/octatrack/core.h, the sites in rate.s).
 */
#include <stdint.h>
#include "octatrack/core.h"

extern int32_t (*const ev_play_increment[])(uint32_t, int32_t);
static int32_t inc_stock[8];

/* rate.s, in the frame interrupt: the track, the increment at the meeting point, and whether the
 * builder reused the stored one (which is then what the handlers returned last time). Returns the
 * increment to store and send. */
int32_t core_play_increment(uint32_t trk, int32_t inc, uint32_t reused)
{
    if (trk > 7)
        return inc;
    if (reused)
        inc = inc_stock[trk];
    else
        inc_stock[trk] = inc;
    for (int32_t (*const *f)(uint32_t, int32_t) = ev_play_increment; *f; ++f)
        inc = (*f)(trk, inc);
    return inc;
}
