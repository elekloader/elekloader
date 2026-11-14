/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core for the Octatrack, OS 1.40C: ev_page, ev_track_select, ev_window and ev_crossfader's handlers
 * (core-ot 0.42, ui.s). */
#include <stdint.h>

extern void (*const ev_page[])(unsigned, unsigned);
extern int (*const ev_track_select[])(unsigned, unsigned, unsigned);
extern void (*const ev_window[])(unsigned, void *, unsigned, unsigned);
extern int (*const ev_crossfader[])(unsigned, unsigned);

void core_page_c(unsigned old, unsigned nw)
{
    for (void (*const *f)(unsigned, unsigned) = ev_page; *f; ++f)
        (*f)(old, nw);
}

/* how 0: before select_track (nonzero = refused, the first taker); how 1: the AUDIO/MIDI swap (watch). */
int core_track_select_c(unsigned old, unsigned nw, unsigned how)
{
    for (int (*const *f)(unsigned, unsigned, unsigned) = ev_track_select; *f; ++f)
        if ((*f)(old, nw, how) && !how)
            return 1;
    return 0;
}

void core_window_c(unsigned op, void *w, unsigned a, unsigned ra)
{
    for (void (*const *f)(unsigned, void *, unsigned, unsigned) = ev_window; *f; ++f)
        (*f)(op, w, a, ra);
}

/* -1 = keep; each handler sees the value the one before chose. */
int core_xfade_c(unsigned value, unsigned source)
{
    int out = -1;
    for (int (*const *f)(unsigned, unsigned) = ev_crossfader; *f; ++f) {
        int v = (*f)(out < 0 ? value : (unsigned)out, source);
        if (v >= 0)
            out = v > 127 ? 127 : v;
    }
    return out;
}
