/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe for trig-events-ot (tests/probe_octatrack_track_trig.py): two ev_track_trig handlers, A
 * (order 10) and B (order 90). A counts and logs each trig it is given and vetoes the tracks in
 * tp_cfg.veto_a; B counts the trigs A let through and vetoes those in tp_cfg.veto_b. The probe
 * writes the configuration into the image's RAM before the sequencer starts. Not a mod to ship. */
#include "octatrack/trig.h"

#define DTCN3 (*(volatile uint32_t *)0xfc07c00c)   /* the DMA timer 3's counter: 132 MHz */

struct cfg {
    uint32_t veto_a;    /* the tracks A vetoes: bit t */
    uint32_t veto_b;    /* the tracks B vetoes */
};
struct cfg tp_cfg;

uint32_t tp_a[OT_TRIG_TRACKS];      /* trigs A was given, by track */
uint32_t tp_b[OT_TRIG_TRACKS];      /* trigs B was given */

/* A's log: the track, the track's flag byte, its machine, the counter and MACSR, in order */
struct ent { uint8_t track, flags, machine, pad; uint32_t t, macsr; };
struct ent tp_log[96];
uint32_t tp_n;

uint32_t tp_ta;                     /* the counter at A's entry, and the longest A-to-B gap seen */
uint32_t tp_gap, tp_gap_min = 0xffffffffu;

int32_t tp_handler_a(int32_t track)
{
    struct ent *e;

    tp_ta = DTCN3;
    tp_a[track & 7]++;
    if (tp_n < 96) {
        e = &tp_log[tp_n++];
        e->track = (uint8_t)track;
        e->flags = OT_TRIG_FLAGS[track];
        e->machine = (uint8_t)ot_trig_machine(track);
        e->t = tp_ta;
        __asm__ volatile("move.l %%macsr,%0" : "=d"(e->macsr));
    }
    return (tp_cfg.veto_a >> track) & 1;
}

int32_t tp_handler_b(int32_t track)
{
    uint32_t gap = DTCN3 - tp_ta;

    tp_b[track & 7]++;
    if (gap > tp_gap)
        tp_gap = gap;
    if (gap < tp_gap_min)
        tp_gap_min = gap;
    return (tp_cfg.veto_b >> track) & 1;
}
